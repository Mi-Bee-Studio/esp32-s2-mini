/*
 * blink — ESP32-S2-Mini（Wemos S2 Mini）基线工程（测试固件）。
 *
 * 板载单色 LED（GPIO15，高电平点亮：IO15→2kΩ→LED→GND）每秒翻转一次；
 * BOOT 键（GPIO0，S2 strapping）按住常亮（交互自检），松开恢复闪烁；
 * 每 10 秒一条心跳日志（uptime/heap），供 serialtap 持续采集验证。
 * 同构拷贝自 esp32-c3-mini/blink（共性先拷贝规范）：Web 维护页（配网/OTA）
 * 与看门狗行为保持一致；控制台走 S2 原生 USB-OTG ROM CDC（无 Serial-JTAG）。
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"
#include "driver/gpio.h"
#include "app_web.h"

static const char *TAG = "BLINK";

#define LED_GPIO  GPIO_NUM_15  // 高电平点亮（原理图 R5 2kΩ 串 LED 到 GND）
#define BOOT_GPIO GPIO_NUM_0

static bool boot_pressed(void)
{
    return gpio_get_level(BOOT_GPIO) == 0; // 按下接地
}

void app_main(void)
{
    const gpio_config_t led = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&led));
    gpio_set_level(LED_GPIO, 0);

    const gpio_config_t btn = {
        .pin_bit_mask = 1ULL << BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn));

    ESP_LOGI(TAG, "blink ready: led=GPIO%d(高有效) boot=GPIO%d heap=%uK",
             LED_GPIO, BOOT_GPIO,
             (unsigned)(esp_get_free_heap_size() / 1024));

    /* SELFTEST 自检行（工作区 AGENTS.md"固件自检行"规范）：一行可 grep 的
     * 开机体检证据，serialtap 按前缀聚合做台架异常发现。blink 的 WiFi
     * 仅为配网/OTA（app_web），不带 wifi 扫描行。PSRAM 探测用 heap_caps
     * 侧总量（无 PSRAM 板返回 0 → none，不依赖 CONFIG_SPIRAM）。 */
    {
        esp_chip_info_t ci;
        esp_chip_info(&ci);
        size_t psz = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        char psram_str[12];
        if (psz) snprintf(psram_str, sizeof(psram_str), "%uMB", (unsigned)(psz >> 20));
        else     strlcpy(psram_str, "none", sizeof(psram_str));
        ESP_LOGI(TAG, "SELFTEST: board=esp32-s2-mini-blink fw=v0.1 chip=%s rev=v%d.%d"
                      " cores=%u psram=%s heap=%uKB",
                 CONFIG_IDF_TARGET, ci.revision / 100, ci.revision % 100,
                 ci.cores, psram_str, (unsigned)(esp_get_free_heap_size() >> 10));
    }

    /* 板端维护页 :80（WiFi 配网 / OTA 刷机 / 状态），自带 APSTA 热点兜底 */
    app_web_init();

    /* 主循环看门狗：1s 一拍喂狗，卡死 >5s 触发 panic 重启自恢复 */
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms = 5000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_err_t werr = esp_task_wdt_init(&wdt_cfg);
    if (werr != ESP_OK && werr != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(werr);
    }
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    int beat = 0;
    bool led_on = false;
    while (true) {
        esp_task_wdt_reset();
        if (boot_pressed()) {
            gpio_set_level(LED_GPIO, 1); // 按住 BOOT 常亮（交互自检）
            led_on = true;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        led_on = !led_on;
        gpio_set_level(LED_GPIO, led_on);
        if (++beat >= 10) {
            beat = 0;
            ESP_LOGI(TAG, "heartbeat uptime=%llds heap=%uK",
                     (long long)(esp_timer_get_time() / 1000000),
                     (unsigned)(esp_get_free_heap_size() / 1024));
        }
    }
}
