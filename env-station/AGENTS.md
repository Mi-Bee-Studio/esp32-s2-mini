# AGENTS.md — env-station（工程级约定）

环境感知节点：AHT20 温湿度 + SR501 PIR + WiFi CSI（#S1 流），Web 维护页
（配网/OTA）沿用 blink 基线。硬件事实见根 [README](../README.zh.md)。

## 硬约束

| 项 | 值 | 说明 |
|----|-----|------|
| ESP-IDF | v6.0 | Xtensa S2 工具链 |
| Flash | 4MB | 分区表 `partitions.csv`：双槽 OTA（ota_0/ota_1），app 偏移 0x20000 |
| PIR (SR501) | GPIO6 | 输入下拉；高 = 有人（模块推挽输出，电位器调延时/灵敏度） |
| AHT20 | I2C 0x38 | SDA=GPIO33、SCL=GPIO35（内部上拉 + 100kHz；探测失败自动对调线序，**以日志为准回改文档**） |
| CSI | `#S1` 家族契约 | 与 wifi-csi-sensing/parser.go 单侧不得改；限频 20Hz + ping 网关 1Hz 激励 |
| 控制台 | USB-OTG ROM CDC | S2 无 USB-Serial-JTAG；烧录先手动 BOOT+RESET 进下载模式 |
| PSRAM | 片内 2MB QSPI | GPIO33-37 空闲（octal 才占用）；未上真机验证，卡死先降 40M/关 SPIRAM |

## 结构约定（同板项目保持同名同构）

- `main/main.c` — 入口：看门狗（最先建，5s panic）、PIR 轮询（500ms）、
  AHT20 采集（5s 一次，失败告警限频 30s）、心跳（10s）；
- `main/app_aht20.c/.h` — AHT20 I2C 驱动（软复位/校准位/CRC8 全做）；
- `main/app_csi.c/.h` — CSI 回调→队列→流任务（`#S1` 打包、限频、激励 ping、
  事件驱动使能/停激励）；
- `main/app_sensors.c/.h` — 传感器快照（main 写 / httpd 读，portMUX）；
- `main/app_web.c/.h` — Web 维护页：状态（含传感器）/ 配网 / OTA / 重启；
- 看门狗：主循环 500ms 喂狗，5s 超时 panic 重启；**panic 必须走
  `CONFIG_ESP_TASK_WDT_PANIC=y`**（代码里 trigger_panic 会被已建 TWDT 吞掉）。

## 构建与烧录

Git Bash 下用 PowerShell 包装脚本构建（见根 README 的 IDF 激活配方）；
`idf.py set-target esp32s2 && idf.py build`。烧录前**先进下载模式**：
按住 BOOT、点按 RESET、松开 BOOT。OTA 包用整份 app bin
（`build/env_station.bin`），从维护页上传即可。
