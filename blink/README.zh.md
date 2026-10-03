# blink — 基线工程（测试固件）

> 同构拷贝自 [`esp32-c3-mini/blink`](../../esp32-c3-mini/blink)（共性先拷贝规范）。

## 功能

- 板载 **LED（GPIO15，高电平点亮）** 每秒翻转一次；
- **BOOT 键（GPIO0）** 按住常亮（交互自检），松开恢复闪烁；
- 每 10 秒一条心跳日志（`uptime` / `heap`），供 serialtap 持续采集验证；
- 板端 **Web 维护页 :80**：WiFi 配网 / 固件 OTA / 重启（未配网时兜底热点
  `blink-s2m` / `12345678` → `192.168.4.1`）；
- **看门狗**：ESP-IDF TWDT 5s 超时 panic（主循环 1s 一拍喂狗）——基线规范强制；
- **OTA**：OTA 双槽（app 起始 `0x20000`），Web 页流式写备用槽 → 校验 → 切槽 → 重启。

## 构建与烧录

```bash
cd blink
idf.py set-target esp32s2
idf.py build
# 先进下载模式：按住 BOOT、点按 RESET、松开 BOOT
idf.py -p COMx flash monitor
```

- 控制台走 S2 原生 **USB-OTG ROM CDC**（无 USB-Serial-JTAG、无桥芯片）；
  复位时端口会重枚举，ROM 阶段启动日志在 USB 上看不到；
- 烧录需要手动 **BOOT + RESET** 手势——本板没有自动下载电路；
- release 附 `flash_blink.sh/.bat`（bootloader + 分区表 + app 三件齐刷）。

## 固件基线规范核对

| 基线 | 状态 |
|------|------|
| 看门狗 | ✅ TWDT 5s panic，主循环喂狗 |
| Web/API OTA | ✅ OTA 双槽 + `POST /ota` 流式写槽 |
