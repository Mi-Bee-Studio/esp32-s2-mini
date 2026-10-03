# ESP32-S2-Mini（Wemos/Lolin S2 Mini）

[中文文档](README.zh.md) | [English](README.md)

[![Build Firmware](https://github.com/Mi-Bee-Studio/esp32-s2-mini/actions/workflows/build.yml/badge.svg)](https://github.com/Mi-Bee-Studio/esp32-s2-mini/actions/workflows/build.yml)

主板仓规范下的一块板。**本仓以"主板为根"组织：**

```
esp32-s2-mini/
├── README.md          # 本文件：这块板的全部硬件信息
└── <project>/         # 每个跑在这块板上的项目一个目录（按能力命名）
    ├── CMakeLists.txt / main/ / sdkconfig.defaults / main/idf_component.yml
    └── README.md      # 项目说明 + 构建/烧录命令
```

规范要点：

- **板目录名** = 板名（kebab-case）；根 README 只写硬件，不写项目内容；
- **每个项目目录独立可编译**：自带完整 ESP-IDF 三件套（顶层 CMakeLists、`main/`、
  `sdkconfig.defaults`），`cd <project> && idf.py build` 直接出固件；
- 项目间不共享代码，需要共性先拷贝，稳定后再议抽组件。

### 固件基线规范（全家桶强制）

1. **看门狗：强制**——任务订阅 ESP-IDF TWDT 并周期喂狗；
2. **Web/API 固件升级（OTA）：硬件允许即强制**——WiFi + 4MB flash 放得下双 OTA 槽，
   每个项目都带板端升级路径。

| 项目 | 看门狗 | Web/API OTA |
|------|--------|-------------|
| blink | ✅ 任务级 TWDT（5s panic） | ✅ 双 OTA 槽 + 流式 `POST /ota` |
| env-station | ✅ TWDT 5s panic（主循环 500ms + CSI 任务） | ✅ 双 OTA 槽 + 流式 `POST /ota` |

---

## Board Overview（板子概要）

引脚图（官方）：[wemos.cc S2 Mini](https://www.wemos.cc/en/latest/s2/s2_mini.html)
——下文的物理排布转录自官方引脚图（`s2_mini_v1.0.0_4_16x9.jpg`）与官方原理图
（`sch_s2_mini_v1.0.0.pdf`），出处已标注。

| 项 | 值 |
|----|-----|
| 芯片 | **ESP32-S2FN4R2** — Xtensa LX7 **单核** @ 240MHz（⚠ v1.0.0 原理图上芯片丝印是 `ESP32-S2FH4`，Wemos 产品页写 FN4R2——以真机 `esptool`/bootloader 报告为准） |
| Flash | 4MB（片内封装） |
| PSRAM | 2MB（片内封装，QSPI）——**尚未上真机验证**，见 Caveats |
| 无线 | 2.4GHz WiFi b/g/n——**无蓝牙**（S2 压根没有） |
| **WiFi CSI** | **支持**。ESP-IDF 里 `SOC_WIFI_CSI_SUPPORT=1`（v6.0.1 源码核实），espressif/esp-csi 也明确全系列含 S2 都支持。开 `CONFIG_ESP_WIFI_CSI_ENABLED=y` + `esp_wifi_set_csi*()` 即可——与家族 C3/S3 感知节点同一套打法 |
| USB | 原生 USB Type-C（**只有 USB-OTG——S2 没有 USB-Serial-JTAG 外设**）。无 USB-UART 桥芯片；控制台走 ROM CDC 驱动（`CONFIG_ESP_CONSOLE_USB_CDC`） |
| UART0 | TX=GPIO43、RX=GPIO44——本板**未引出** |
| 板载 LED | 独立 LED 接 **GPIO15**（高电平点亮：IO15 → 2kΩ → LED → GND，见原理图） |
| 按键 | BOOT = GPIO0（按住进下载模式），RESET = EN（CHIP_PU） |
| ADC | ADC1 = GPIO1–10（可与 WiFi 同开）；ADC2 = GPIO11–20（**WiFi 开启期间不可用**——芯片级限制） |
| DAC | DAC1 = GPIO17，DAC2 = GPIO18 |
| 供电 | 3.3V LDO（ME6211C33）；`VBUS` 引脚 = USB 5V；排针上有 `3V3`/`GND` |
| 引出 | **27 个用户 GPIO**（1,2,4,6,8,10,13,14,15,16,17,18,21,33–40），2×16 铸钥孔；**未引出**：19/20（USB）、26–32（flash）、41–46（含 strapping 45/46、UART0 43/44） |
| 尺寸 | 34.3 × 25.4 mm（LOLIN D1 mini 形态，可插 D1 mini 扩展板——3.3V 逻辑） |
| 出厂 | 默认带 MicroPython；本仓用 **ESP-IDF**（v6.0） |

## Pinout Diagram（USB-C 朝上、正面/元件面视角）

```
      外排       内排    ┌─ USB-C ─┐    内排       外排
      VBUS ◎├──  15  ──┤ [BOOT]  ├──  14  ──┤◎ 3V3   ← LED 在 USB 旁边，
       GND ◎│    GND   │  (LED   │   13     │ 12        丝印 "15"
        16 ◎│    17    │   =15)  │   10     │ 11
        18 ◎│    21    │  ┌────┐ │    8     │  9
        33 ◎│    34    │  │S2  │ │    6     │  7
        35 ◎│    36    │  │FN4R2    │    4     │  5
        37 ◎│    38    │  └────┘ │    2     │  3
        39 ◎│    40    │ PCB天线      1     │ EN (RESET)
             └──────────┴─────────┴──────────┘
              左边（2×8）          右边（2×8）
```

要点（按官方引脚图 + 原理图）：

- 每条长边**内外两排**各 8 个焊盘（共 32 盘，其中 27 个是 GPIO）；
- **左边缘**外排（USB 端 → 下）：`VBUS, GND, 16, 18, 33, 35, 37, 39`；
  内排：`15, GND, 17, 21, 34, 36, 38, 40`；
- **右边缘**内排（USB 端 → 下）：`14, 13, 10, 8, 6, 4, 2, 1`；
  外排：`3V3, 12, 11, 9, 7, 5, 3, EN`；
- GPIO15 复用 `XTAL_32K_P`、GPIO16 复用 `XTAL_32K_N`（仅在需要外部 32kHz 晶振时相关——LED 占着 15）；
- GPIO33–37 是 SPIIO4–7/SPIDQS——本板**空闲可用**，因为片内 PSRAM 是 QSPI（只有 *octal* PSRAM 变体才占用它们）；
- GPIO19/20 = USB D-/D+，不要挪用；GPIO0 = BOOT 键（上电电平决定启动模式，作输入时要留意上电态）。

## Caveats（坑）

- **没有 USB-Serial-JTAG（与 C3/S3 家族板最大的不同）**：S2 只有 USB-OTG。控制台走
  ROM CDC（`CONFIG_ESP_CONSOLE_USB_CDC=y`）。连带后果：
  - ROM bootloader 阶段日志仍走 **UART0（GPIO43/44，本板未引出）**——USB 口上看不到
    最早的启动日志，要等 app 的 CDC 起来才有输出；
  - ROM CDC 控制台**与 TinyUSB 栈不兼容**（IDF Kconfig 强制互斥）——别加会抢占 USB
    设备的 `esp_tinyusb` 类组件；
  - `idf.py monitor` 可用，但复位时端口会重枚举（ROM CDC ↔ app CDC 是两个不同的 USB
    设备）——每次重启/panic 都会有一次短暂掉线，属正常现象。
- **进下载模式：没有自动下载电路。** 按住 **BOOT**（GPIO0）、点按 **RESET**、松开
  BOOT → ROM 以 CDC 设备枚举，esptool 即可烧录。不做这个手势直接
  `idf.py -p COMx flash` 会 sync 失败。（C3/S3 板上 serialtap 的 DTR/RTS 自动复位
  戏法在本板不适用——板上没有把 DTR/RTS 接到 EN/IO0 的电路。）
- **PSRAM 未上真机验证（2026-10-03）**：标称片内 2MB QSPI；`sdkconfig.defaults` 已开
  `CONFIG_SPIRAM=y` + QUAD @ 80MHz。若真机在 PSRAM 初始化处反复重启，先降
  `SPIRAM_SPEED_40M`，再试 `CONFIG_SPIRAM=n`——与 esp32-s3-zero 的 Octal-PSRAM 教训
  同一套排查（PSRAM 初始化卡死会让 USB 彻底消失；断电重插 + BOOT 进下载模式可救）。
- **GPIO11–20 = ADC2**：WiFi 开启时读数不可用（芯片级约束，不是走线问题）。WiFi 项目
  里用 ADC1（GPIO1–10）。
- 原理图芯片标 `ESP32-S2FH4`（无 PSRAM）而产品页写 `ESP32-S2FN4R2`——原理图大概率早于
  换片。上真机后先看 bootloader/esptool 打印（有无 `Embedded PSRAM 2MB`）再决定是否
  依赖 PSRAM。
- D1 mini 扩展板兼容是这块板的卖点——但老扩展板可能按 5V 逻辑设计，本板是 3.3V。

## Serialtap（中间件）接入点

- 本板枚举为普通 USB CDC-ACM（没有 "jtag" 命名风味）——serialtap 的
  发现/命名/抓取/透传照常可用；经代理烧录需要上面的手动 BOOT+RESET 手势（板上没有
  串口级自动复位可自动化）；
- 4MB flash + 家族双 OTA 布局——烧录耗时可参考 C3 板同量级；
- 单色 LED（GPIO15）可作设备状态灯；serialtap/homepulse 可经代理推开关量（板端项目侧实现）。

## Project Index（项目索引）

| 项目 | 说明 |
|------|------|
| [blink](blink/README.zh.md) | 基线工程（测试固件）：LED（GPIO15）闪烁 + BOOT 交互 + 心跳日志 + Web 维护页（配网/OTA）+ TWDT 看门狗 |
| [env-station](env-station/README.zh.md) | 环境感知节点（2026-10-03 已接线）：AHT20 温湿度（I2C SDA=33/SCL=35）+ SR501 PIR（GPIO6）+ WiFi CSI `#S1` 流（20Hz + 网关 ping 激励）+ Web 维护页（配网/OTA）+ TWDT |
