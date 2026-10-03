/* app_aht20 — AHT20 温湿度驱动（I2C master，0x38）。
 * 时序按官方 datasheet：上电 ≥40ms（main 里 WiFi 初始化早够）→
 * 软复位 0xBA → 查校准位(bit3) → 测量 0xAC 0x33 0x00 等 80ms → 7 字节，
 * 状态忙位 bit7，20bit 湿度 + 20bit 温度 + CRC8(poly 0x31, init 0xFF)。
 * 内部上拉可撑 100kHz 短线；模块自带 4.7-10k 上拉更稳。 */
#include "app_aht20.h"

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "aht20";

#define AHT20_ADDR        0x38
#define AHT20_TIMEOUT_MS  100

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static bool s_crc_warned;

esp_err_t aht20_start(int sda_io, int scl_io)
{
    /* 换线序重试时先拆旧 bus（device 挂在 bus 上，先卸 device） */
    if (s_bus) {
        i2c_master_bus_rm_device(s_dev);
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = 0,
        .sda_io_num = sda_io,
        .scl_io_num = scl_io,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        return err;
    }
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AHT20_ADDR,
        .scl_speed_hz = 100 * 1000,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        return err;
    }

    /* 探测：地址 ACK（读状态字节） */
    uint8_t status = 0;
    err = i2c_master_receive(s_dev, &status, 1, AHT20_TIMEOUT_MS);
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(s_dev);
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        return err; /* 无 ACK：地址/线序不对或没上拉 */
    }

    /* 软复位 0xBA，等 20ms */
    const uint8_t rst = 0xBA;
    i2c_master_transmit(s_dev, &rst, 1, AHT20_TIMEOUT_MS);
    vTaskDelay(pdMS_TO_TICKS(20));
    i2c_master_receive(s_dev, &status, 1, AHT20_TIMEOUT_MS);

    /* 校准位（status bit3）未使能则发初始化 0xBE 0x08 0x00 */
    if (!(status & 0x08)) {
        const uint8_t cal[3] = { 0xBE, 0x08, 0x00 };
        i2c_master_transmit(s_dev, cal, sizeof(cal), AHT20_TIMEOUT_MS);
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_LOGI(TAG, "AHT20 校准位未使能，已发初始化命令");
    }
    ESP_LOGI(TAG, "AHT20 就绪 @0x%02X（SDA=%d SCL=%d）status=0x%02X",
             AHT20_ADDR, sda_io, scl_io, status);
    return ESP_OK;
}

static uint8_t crc8_aht20(const uint8_t *d, int n)
{
    uint8_t crc = 0xFF;
    while (n--) {
        crc ^= *d++;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

esp_err_t aht20_read(float *temp_c, float *rh)
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t trig[3] = { 0xAC, 0x33, 0x00 };
    esp_err_t err = i2c_master_transmit(s_dev, trig, sizeof(trig), AHT20_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(80)); /* 测量完成 ≤80ms */

    uint8_t d[7] = { 0 };
    err = i2c_master_receive(s_dev, d, sizeof(d), AHT20_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    if (d[0] & 0x80) { /* 忙位：测量未完（时钟被抢占等） */
        return ESP_ERR_INVALID_STATE;
    }
    if (crc8_aht20(&d[1], 6) != d[6]) {
        if (!s_crc_warned) {
            s_crc_warned = true;
            ESP_LOGW(TAG, "AHT20 CRC 校验失败（后续静默，恢复成功不再提示）");
        }
        return ESP_ERR_INVALID_CRC;
    }
    const uint32_t raw_h = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
    const uint32_t raw_t = (((uint32_t)d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
    *rh = raw_h * 100.0f / 1048576.0f;
    *temp_c = raw_t * 200.0f / 1048576.0f - 50.0f;
    s_crc_warned = false; /* 读取恢复正常后重新使能告警 */
    return ESP_OK;
}
