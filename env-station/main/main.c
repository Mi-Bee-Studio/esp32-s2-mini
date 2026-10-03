/* env-station — ESP32-S2-Mini（Wemos S2 Mini）环境感知节点。
 *
 * 接线（与仓 README 同步）：SR501 PIR OUT → GPIO6（高 = 有人活动，模块
 * 推挽输出免上拉）；AHT20 I2C：SDA=GPIO33、SCL=GPIO35（内部上拉 +
 * 模块自带上拉，100kHz；探测失败自动对调线序重试——接线顺序以日志为准）。
 * WiFi CSI 采集走家族 #S1 契约（app_csi）；Web 维护页（配网/OTA）沿用
 * blink 基线（app_web）。同构拷贝自 esp32-s3-zero/env-station 思路。
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "driver/gpio.h"
#include "app_web.h"
#include "app_aht20.h"
#include "app_csi.h"
#include "app_sensors.h"

static const char *TAG = "ENV";

#define PIR_GPIO GPIO_NUM_6
#define AHT_SDA  33
#define AHT_SCL  35

void app_main(void)
{
    ESP_LOGI(TAG, "env-station boot: pir=GPIO%d aht20 sda=%d/scl=%d(失败自动对调) csi=on",
             PIR_GPIO, AHT_SDA, AHT_SCL);

    /* 看门狗最先建（5s panic；csi 流任务会自挂，主循环 500ms 一拍喂狗） */
    const esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = 5000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_err_t werr = esp_task_wdt_init(&wdt_cfg);
    if (werr != ESP_OK && werr != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(werr);
    }
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    const gpio_config_t pir = {
        .pin_bit_mask = 1ULL << PIR_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE, /* 模块推挽驱动；下拉只为断线兜底 */
    };
    ESP_ERROR_CHECK(gpio_config(&pir));

    /* 板端维护页 :80（WiFi 配网 / OTA 刷机 / 状态），自带 APSTA 热点兜底。
     * 内部会初始化 NVS/netif/event loop，CSI 的事件注册在其后也无冲突。 */
    app_web_init();
    app_csi_init();

    /* AHT20：先按标称线序 SDA=33/SCL=35，无 ACK 自动对调重试一次 */
    esp_err_t aerr = aht20_start(AHT_SDA, AHT_SCL);
    int sda = AHT_SDA, scl = AHT_SCL;
    if (aerr != ESP_OK) {
        ESP_LOGW(TAG, "AHT20 在 SDA=%d/SCL=%d 未探测到（%s），对调线序重试",
                 AHT_SDA, AHT_SCL, esp_err_to_name(aerr));
        sda = AHT_SCL;
        scl = AHT_SDA;
        aerr = aht20_start(sda, scl);
        if (aerr == ESP_OK) {
            ESP_LOGI(TAG, "AHT20 是对调线序找到的：SDA=%d/SCL=%d——请把实物接线与文档改成这个顺序",
                     sda, scl);
        }
    }
    if (aerr != ESP_OK) {
        ESP_LOGE(TAG, "AHT20 两种线序都探测失败，温湿度不可用（检查接线/模块供电 3.3V）");
    }

    bool last_motion = gpio_get_level(PIR_GPIO);
    env_set_motion(last_motion);
    int64_t last_aht = 0;
    int64_t last_aht_err = -30000;
    int64_t last_hb = 0;

    while (true) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(500));

        const bool motion = gpio_get_level(PIR_GPIO);
        if (motion != last_motion) {
            ESP_LOGI(TAG, "PIR %s", motion ? "检测到活动 ↑" : "恢复静止 ↓");
            env_set_motion(motion);
            last_motion = motion;
        }

        const int64_t now = esp_timer_get_time() / 1000;
        if (aerr == ESP_OK && now - last_aht >= 5000) {
            float t, rh;
            last_aht = now;
            const esp_err_t rerr = aht20_read(&t, &rh);
            if (rerr == ESP_OK) {
                env_update(t, rh);
            } else if (now - last_aht_err >= 30000) { /* 失败告警限频 30s */
                last_aht_err = now;
                ESP_LOGW(TAG, "AHT20 读取失败（%s），30s 后再告警", esp_err_to_name(rerr));
            }
        }
        if (now - last_hb >= 10000) {
            env_snapshot_t s;
            env_get(&s);
            last_hb = now;
            if (s.aht_ok) {
                ESP_LOGI(TAG, "heartbeat uptime=%llds heap=%uK T=%.1f°C RH=%.1f%% pir=%d csi=%u(drop %u)",
                         (long long)(now / 1000),
                         (unsigned)(esp_get_free_heap_size() / 1024),
                         s.temp_c, s.rh, s.motion,
                         (unsigned)app_csi_total(), (unsigned)app_csi_dropped());
            } else {
                ESP_LOGI(TAG, "heartbeat uptime=%llds heap=%uK T=-- RH=-- pir=%d csi=%u(drop %u)",
                         (long long)(now / 1000),
                         (unsigned)(esp_get_free_heap_size() / 1024),
                         s.motion,
                         (unsigned)app_csi_total(), (unsigned)app_csi_dropped());
            }
        }
    }
}
