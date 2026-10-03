#pragma once

#include <stdint.h>

/* WiFi CSI 采集（#S1 流，家族契约同 luatos-esp32c3/wifi-csi-sensing）：
 *   会话头 "#S1-HELLO nsc=%d sel=..."；
 *   数据行 "#S1 seq t_ms rssi hex64"（16 子载波 × (I,Q) 各 1 字节 hex）。
 * 该格式是 homepulse/internal/sense/parser.go 的输入，单侧不得改。
 * init 注册事件：STA 关联 → 使能 CSI；拿到 IP → ping 网关激励；
 * 断开 → 停激励。 */
void app_csi_init(void);

/* 诊断计数（回调总数 / 队列满丢弃数） */
uint32_t app_csi_total(void);
uint32_t app_csi_dropped(void);
