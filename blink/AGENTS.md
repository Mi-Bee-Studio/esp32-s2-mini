# AGENTS.md — blink（工程级约定）

基线工程：验证这块板的最小闭环（LED / BOOT 键 / 心跳日志 / USB CDC），
也是板端标配能力（维护页/OTA/看门狗）的最简参考实现。
硬件事实见根 [README](../README.zh.md)。

## 硬约束

| 项 | 值 | 说明 |
|----|-----|------|
| ESP-IDF | v6.0 | Xtensa S2 工具链 |
| Flash | 4MB | 分区表 `partitions.csv`：双槽 OTA（ota_0/ota_1），app 偏移 0x20000 |
| LED | GPIO15（板载） | **高电平点亮**（IO15→2kΩ→LED→GND），普通 GPIO 驱动 |
| BOOT 键 | GPIO0 | 输入上拉，按下为 0；S2 strapping，上电电平决定启动模式 |
| 控制台 | **USB-OTG ROM CDC** | S2 没有 USB-Serial-JTAG；`CONFIG_ESP_CONSOLE_USB_CDC=y`，与 TinyUSB 互斥 |
| PSRAM | 片内 2MB QSPI | `CONFIG_SPIRAM=y` QUAD@80M；⚠ 未上真机验证，卡死先降 40M/关 PSRAM |

## 结构约定（同板项目保持同名同构）

- `main/main.c` — 入口：心跳循环、看门狗、启动自检（heap 打印）；
- `main/app_web.c/.h` — 板端 Web 维护页（:80）：状态 / WiFi 配网 / OTA 上传 /
  重启，前后端一体内嵌单页；自带 APSTA 救援热点（未配网时 `blink-s2m`/
  `12345678` → 192.168.4.1），STA 关联后 AP 自动跟随信道；
- 看门狗：主循环 1s 喂狗，5s 超时 panic 重启；**panic 必须走
  `CONFIG_ESP_TASK_WDT_PANIC=y`**（代码里 trigger_panic 会被已建 TWDT 吞掉）。

## 构建与烧录

Git Bash 下 `idf.py set-target esp32s2 && idf.py build`（工程已设
`__CHECK_PYTHON 0` 绕过 MSys 的 Python 预检）。**烧录前先进下载模式：
按住 BOOT、点按 RESET、松开 BOOT**（本板无自动下载电路，DTR/RTS 接不到
EN/IO0；ROM CDC 在下载模式直接枚举，esptool 即可烧）。串口被代理类工具
占用时先停代理。OTA 包用整份 app bin（`build/blink.bin`），从维护页上传即可。
