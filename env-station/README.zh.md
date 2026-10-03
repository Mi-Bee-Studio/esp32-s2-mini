# env-station — 环境感知节点（AHT20 + SR501 + WiFi CSI）

> 同构思路拷贝自 [`esp32-s3-zero/env-station`](../../esp32-s3-zero/env-station)（共性先拷贝规范）；
> Web 维护页与 OTA 直接沿用 [blink](../blink/README.zh.md) 基线代码。

## 功能

- **AHT20 温湿度**（I2C 0x38）：每 5s 采集一次，CRC8 校验；
- **SR501 PIR 人体感应**（GPIO6）：电平变化即日志 + 状态上报（模块自身带
  延时/灵敏度电位器，固件只读电平：高 = 有人活动）；
- **WiFi CSI 采集**：STA 关联后使能，`#S1` 遥测流（家族契约，见下）+
  ping 网关激励（1Hz 回包 = 稳定下行 CSI 源），限频 20Hz；
- 板端 **Web 维护页 :80**：状态（温湿度/PIR/CSI 计数）/ WiFi 配网 / OTA / 重启
  （未配网时兜底热点 `env-s2m` / `12345678` → `192.168.4.1`）；
- **看门狗**：TWDT 5s panic，主循环 500ms 喂狗（CSI 流任务自挂自喂）；
- **OTA**：双 OTA 槽（app 起始 `0x20000`），Web 页流式写备用槽 → 校验 → 切槽 → 重启。

## 接线（2026-10-03 实物）

| 外设 | 引脚 | 板上 GPIO |
|------|------|-----------|
| SR501 PIR OUT | → | **GPIO6**（模块 VCC 5V/GND） |
| AHT20 SDA | → | **GPIO33** |
| AHT20 SCL | → | **GPIO35** |

- AHT20 线序按 "SDA、SCL" 口述顺序接线；固件探测失败会**自动对调重试**并在
  日志里报出实际生效的线序——以日志为准回改本文档；
- AHT20 模块一般自带上拉（4.7–10kΩ）；固件同时开了 GPIO 内部上拉兜底（100kHz 短线够用）；
- GPIO33–37 在 QSPI PSRAM（本板是 QSPI）下空闲可用，只有 octal PSRAM 才占用。

## CSI `#S1` 契约

与 [`luatos-esp32c3/air101-lcd/wifi-csi-sensing`](../../luatos-esp32c3/air101-lcd/wifi-csi-sensing/README.zh.md)
同构（`homepulse/internal/sense/parser.go` 的输入，**单侧不得改**）：

- 会话头：`#S1-HELLO nsc=<子载波总数> sel=<16 个选中下标> rate=20`
- 数据行：`#S1 <seq> <t_ms> <rssi> <hex64>`（16 子载波 × (I,Q) 各 1 字节的 hex）
- 16 个子载波均匀取自 `[nsc/8, nsc*7/8]`，避开边缘空载波与直流；
- 激励：拿到 IP 后 ping 网关 1Hz（ACK/回包是下行单播帧 → 稳定 CSI 源）。

## 构建与烧录

```bash
cd env-station
idf.py set-target esp32s2
idf.py build
# 先进下载模式：按住 BOOT、点按 RESET、松开 BOOT
idf.py -p COMx flash monitor
```

- 控制台走 S2 原生 **USB-OTG ROM CDC**（无 USB-Serial-JTAG、无桥芯片）；
- 烧录需要手动 **BOOT + RESET** 手势（本板无自动下载电路）；
- WiFi 配网：手机连热点 `env-s2m`/`12345678` → `http://192.168.4.1`，或已知
  STA IP 直接访问维护页。

## 固件基线规范核对

| 基线 | 状态 |
|------|------|
| 看门狗 | ✅ TWDT 5s panic，主循环 500ms 喂狗 + CSI 流任务自挂 |
| Web/API OTA | ✅ OTA 双槽 + `POST /ota` 流式写槽 |
