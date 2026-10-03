/* app_csi — WiFi CSI 采集（#S1 流）。同构拷贝自
 * luatos-esp32c3/air101-lcd/wifi-csi-sensing 的 app_sense.c（共性先拷贝），
 * 裁掉本地存在估计与 JSON 协议，保留 #S1 家族契约与激励 ping。
 * 注意：勿用 WIFI_PS_NONE —— 繁忙信道上帧处理会吃满单核饿死 IDLE
 * （家族实测教训）；采样率靠 ping 激励（TX 后无线电清醒）。 */
#include "app_csi.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "lwip/sockets.h"
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_task_wdt.h"

static const char *TAG = "csi";

#define CSI_NSEL        16   /* 选定子载波数（= hex64 长度/4） */
#define CSI_QUEUE_LEN   64
#define CSI_TASK_STACK  4096
#define CSI_TASK_PRIO   3
#define CSI_MAX_HZ      20   /* #S1 限频 */
#define CSI_STIMULUS_HZ 1    /* ping 网关激励频率 */

typedef struct {
    int64_t t_ms;
    int8_t  rssi;
    int8_t  iq[CSI_NSEL * 2]; /* 每子载波 (I,Q) */
    int     nsc;              /* 芯片报告的复数子载波总数（会话头用） */
} csi_rec_t;

static QueueHandle_t s_queue;
static volatile bool s_csi_on;          /* CSI 已使能（STA 已关联） */
static volatile uint32_t s_total, s_drop;
static esp_ping_handle_t s_ping;

/* ---------------------------------------------------------------------------
 * CSI 接收回调（WiFi 任务上下文：只做选取与入队，绝不阻塞/打印）
 * --------------------------------------------------------------------------- */
static void csi_rx_cb(void *ctx, wifi_csi_info_t *info)
{
    (void)ctx;
    const int nsc = info->len / 2;
    if (nsc < CSI_NSEL) {
        return;
    }
    csi_rec_t rec = {
        .t_ms = esp_timer_get_time() / 1000,
        .rssi = info->rx_ctrl.rssi,
        .nsc  = nsc,
    };
    /* 16 个子载波均匀取自 [nsc/8, nsc*7/8]：避开边缘空载波与直流 */
    const int lo = nsc / 8;
    const int span = (nsc * 3) / 4;
    for (int k = 0; k < CSI_NSEL; k++) {
        int idx = lo + (k * span) / (CSI_NSEL - 1);
        if (idx >= nsc) {
            idx = nsc - 1;
        }
        rec.iq[2 * k]     = info->buf[2 * idx];
        rec.iq[2 * k + 1] = info->buf[2 * idx + 1];
    }
    s_total++;
    if (xQueueSend(s_queue, &rec, 0) != pdTRUE) {
        s_drop++; /* 队列满丢弃；homepulse 侧重采样对 <5% 缺样鲁棒 */
    }
}

/* ---------------------------------------------------------------------------
 * 主动激励：ping 网关（回包 = 下行单播数据帧 → 稳定 CSI 源）
 * --------------------------------------------------------------------------- */
static void stimulus_start(void)
{
    if (s_ping) {
        return;
    }
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        return;
    }
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.gw.addr == 0) {
        return;
    }
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    cfg.count = 0; /* 无限 */
    cfg.interval_ms = 1000 / CSI_STIMULUS_HZ;
    cfg.timeout_ms = 1000;
    cfg.data_size = 8; /* 小包即可 */
    char gws[16];
    snprintf(gws, sizeof(gws), IPSTR, IP2STR(&ip.gw));
    if (ipaddr_aton(gws, &cfg.target_addr) == 0) {
        return;
    }
    esp_ping_callbacks_t cbs = { 0 };
    if (esp_ping_new_session(&cfg, &cbs, &s_ping) == ESP_OK) {
        esp_ping_start(s_ping);
        ESP_LOGI(TAG, "激励 ping %s 每 %d ms（回包=下行数据帧 → CSI）",
                 gws, 1000 / CSI_STIMULUS_HZ);
    } else {
        s_ping = NULL;
        ESP_LOGW(TAG, "ping 会话创建失败");
    }
}

static void stimulus_stop(void)
{
    if (s_ping) {
        esp_ping_stop(s_ping);
        esp_ping_delete_session(s_ping);
        s_ping = NULL;
    }
}

/* CSI 使能（STA 关联后调用；断开后重关联会再来一次） */
static void csi_enable(void)
{
    if (s_csi_on) {
        return;
    }
    const wifi_csi_config_t cfg = {
        .lltf_en = true,
        .htltf_en = true,
        .stbc_htltf2_en = false,
        .ltf_merge_en = true,
        .channel_filter_en = false, /* 保持子载波独立性（感知分析需要） */
        .manu_scale = false,
        .dump_ack_en = true,        /* 激励模式靠 ACK 帧出 CSI */
    };
    esp_err_t ret = esp_wifi_set_csi_config(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "CSI 配置失败: %s（sdkconfig 需 CONFIG_ESP_WIFI_CSI_ENABLED）",
                 esp_err_to_name(ret));
        return;
    }
    ret = esp_wifi_set_csi_rx_cb(csi_rx_cb, NULL);
    if (ret == ESP_OK) {
        ret = esp_wifi_set_csi(true);
    }
    if (ret == ESP_OK) {
        s_csi_on = true;
        ESP_LOGI(TAG, "CSI 采集已使能");
    } else {
        ESP_LOGW(TAG, "CSI 使能失败: %s", esp_err_to_name(ret));
    }
}

static void on_evt(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        csi_enable();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_csi_on = false;
        stimulus_stop();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        stimulus_start();
    }
}

/* ---------------------------------------------------------------------------
 * 流任务：出队 → 限频 → "#S1" 行 → 控制台（USB CDC）
 * --------------------------------------------------------------------------- */
static const char HEXD[] = "0123456789abcdef";

static void csi_stream_task(void *arg)
{
    (void)arg;
    static char line[96];
    static char hexbuf[CSI_NSEL * 4 + 1];
    bool hello_sent = false;
    bool prev_on = false;
    int64_t last_sent_ms = 0;
    uint32_t seq = 0;

    /* 挂看门狗：输出路径死锁时遥测不能无声停摆（家族 03:01 事故教训）；
     * 队列等待 200ms 限时，空闲也周期喂狗 */
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    ESP_LOGI(TAG, "csi 流任务启动（限频 %dHz 激励 %dHz）", CSI_MAX_HZ, CSI_STIMULUS_HZ);

    while (true) {
        esp_task_wdt_reset();
        csi_rec_t rec;
        if (xQueueReceive(s_queue, &rec, pdMS_TO_TICKS(200)) != pdTRUE) {
            continue;
        }
        if (s_csi_on && !prev_on) {
            hello_sent = false; /* 新会话（重关联后）重发会话头 */
        }
        prev_on = s_csi_on;
        if (!s_csi_on) {
            continue;
        }
        if (rec.t_ms - last_sent_ms < 1000 / CSI_MAX_HZ) {
            continue; /* 超限丢弃（时间戳连续性由 PC 侧重采样兜底） */
        }
        last_sent_ms = rec.t_ms;

        if (!hello_sent) {
            const int lo = rec.nsc / 8;
            const int span = (rec.nsc * 3) / 4;
            int n = snprintf(line, sizeof(line), "#S1-HELLO nsc=%d sel=", rec.nsc);
            for (int k = 0; k < CSI_NSEL && n < (int)sizeof(line) - 6; k++) {
                int idx = lo + (k * span) / (CSI_NSEL - 1);
                if (idx >= rec.nsc) {
                    idx = rec.nsc - 1;
                }
                n += snprintf(line + n, sizeof(line) - n, k ? ",%d" : "%d", idx);
            }
            snprintf(line + n, sizeof(line) - n, " rate=%d\n", CSI_MAX_HZ);
            printf("%s", line);
            hello_sent = true;
        }

        for (int i = 0; i < CSI_NSEL * 2; i++) {
            const uint8_t v = (uint8_t)rec.iq[i];
            hexbuf[2 * i]     = HEXD[v >> 4];
            hexbuf[2 * i + 1] = HEXD[v & 0xF];
        }
        hexbuf[CSI_NSEL * 4] = '\0';
        snprintf(line, sizeof(line), "#S1 %lu %lld %d %s\n",
                 (unsigned long)seq, (long long)rec.t_ms, (int)rec.rssi, hexbuf);
        printf("%s", line);
        seq++;
    }
}

void app_csi_init(void)
{
    s_queue = xQueueCreate(CSI_QUEUE_LEN, sizeof(csi_rec_t));
    xTaskCreate(csi_stream_task, "csi_stream", CSI_TASK_STACK, NULL, CSI_TASK_PRIO, NULL);
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_evt, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_evt, NULL));
}

uint32_t app_csi_total(void)
{
    return s_total;
}

uint32_t app_csi_dropped(void)
{
    return s_drop;
}
