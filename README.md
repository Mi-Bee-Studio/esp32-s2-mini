# ESP32-S2-Mini (Wemos/Lolin S2 Mini)

[中文文档](README.zh.md) | [English](README.md)

[![Build Firmware](https://github.com/Mi-Bee-Studio/esp32-s2-mini/actions/workflows/build.yml/badge.svg)](https://github.com/Mi-Bee-Studio/esp32-s2-mini/actions/workflows/build.yml)

A board under the board-centric repo convention. **This repo is organized with the board as root:**

```
esp32-s2-mini/
├── README.md          # this file: all hardware info for this board
└── <project>/         # one directory per project built on this board (named by capability)
    ├── CMakeLists.txt / main/ / sdkconfig.defaults / main/idf_component.yml
    └── README.md      # project description + build/flash commands
```

Key points of the convention:

- **Board directory name** = board name (kebab-case); the root README covers hardware only, never project content;
- **Each project directory builds standalone**: it ships the full ESP-IDF project trio (top-level CMakeLists,
  `main/`, `sdkconfig.defaults`); `cd <project> && idf.py build` produces the firmware;
- Projects share no code; when commonality is needed, copy first, and consider extracting a shared component only once things stabilize.

### Firmware baseline norms (mandatory fleet-wide)

1. **Watchdog: mandatory.** Tasks subscribe to the ESP-IDF TWDT and feed it periodically;
2. **Web/API firmware upgrade (OTA): mandatory where the hardware allows.** WiFi plus
   4 MB flash fit dual OTA slots, so every project ships a web upgrade path.

| Project | Watchdog | Web/API OTA |
|---------|----------|-------------|
| blink | ✅ per-task TWDT (5 s panic) | ✅ dual OTA slots + streaming `POST /ota` |

---

## Board Overview

Pinout image (official): [wemos.cc S2 Mini](https://www.wemos.cc/en/latest/s2/s2_mini.html)
— the physical order documented below is transcribed from the official pinout diagram
(`s2_mini_v1.0.0_4_16x9.jpg`) and the official schematic (`sch_s2_mini_v1.0.0.pdf`).

| Item | Value |
|------|-------|
| Module/chip | **ESP32-S2FN4R2** — Xtensa LX7 **single-core** @ 240 MHz (⚠ the v1.0.0 schematic labels the chip `ESP32-S2FH4`; the Wemos product page says FN4R2 — trust what `esptool`/bootloader reports on the real unit) |
| Flash | 4 MB (embedded in-chip) |
| PSRAM | 2 MB (embedded in-chip, QSPI) — **not yet verified on our hardware**, see Caveats |
| Wireless | 2.4 GHz WiFi b/g/n — **no Bluetooth** (S2 has none at all) |
| **WiFi CSI** | **Supported.** `SOC_WIFI_CSI_SUPPORT=1` in ESP-IDF (verified in the v6.0.1 tree), and espressif/esp-csi states all ESP32 series incl. S2 support CSI. Enable with `CONFIG_ESP_WIFI_CSI_ENABLED=y` + `esp_wifi_set_csi*()` — the same pattern as the family's CSI sensing nodes (C3/S3) |
| USB | Native USB Type-C (**USB-OTG only — S2 has no USB-Serial-JTAG peripheral**). No USB-UART bridge chip; console runs on the ROM CDC driver (`CONFIG_ESP_CONSOLE_USB_CDC`) |
| UART0 | TX=GPIO43, RX=GPIO44 — **not broken out** on this board |
| Onboard LED | Discrete LED on **GPIO15** (active-high: IO15 → 2kΩ → LED → GND per schematic) |
| Buttons | BOOT = GPIO0 (hold to enter download mode), RESET = EN (CHIP_PU) |
| ADC | ADC1 = GPIO1–10 (works with WiFi on), ADC2 = GPIO11–20 (**unusable while WiFi is active** — chip-level limit) |
| DAC | DAC1 = GPIO17, DAC2 = GPIO18 |
| Power | 3.3 V LDO (ME6211C33); `VBUS` pin = USB 5 V; `3V3`/`GND` on the header |
| Breakout | **27 user GPIOs** (1,2,4,6,8,10,13,14,15,16,17,18,21,33–40) on 2×16 castellated pads; **not** broken out: 19/20 (USB), 26–32 (flash), 41–46 (incl. strapping 45/46, UART0 43/44) |
| Dimensions | 34.3 × 25.4 mm (LOLIN D1 mini form factor, accepts D1 mini shields — 3.3 V logic) |
| Factory | Ships with MicroPython by default; this repo uses **ESP-IDF** (v6.0) |

## Pinout Diagram (USB-C pointing up, front/component-side view)

```
      outer      inner    ┌─ USB-C ─┐    inner      outer
      VBUS ◎├──  15  ──┤ [BOOT]  ├──  14  ──┤◎ 3V3   ← LED is next to the USB,
       GND ◎│    GND   │  (LED   │   13     │ 12        silkscreen "15"
        16 ◎│    17    │   =15)  │   10     │ 11
        18 ◎│    21    │  ┌────┐ │    8     │  9
        33 ◎│    34    │  │S2  │ │    6     │  7
        35 ◎│    36    │  │FN4R2    │    4     │  5
        37 ◎│    38    │  └────┘ │    2     │  3
        39 ◎│    40    │ PCB antenna  1     │ EN (RESET)
             └──────────┴─────────┴──────────┘
              left edge (2×8)      right edge (2×8)
```

Key points (per the official pinout image + schematic):

- Each long edge carries **two columns** of 8 pads (32 pads total, 27 of them GPIO);
- **Left edge** outer (USB end → bottom): `VBUS, GND, 16, 18, 33, 35, 37, 39`;
  inner: `15, GND, 17, 21, 34, 36, 38, 40`;
- **Right edge** inner (USB end → bottom): `14, 13, 10, 8, 6, 4, 2, 1`;
  outer: `3V3, 12, 11, 9, 7, 5, 3, EN`;
- GPIO15 doubles as `XTAL_32K_P` and GPIO16 as `XTAL_32K_N` (only relevant if you need an external 32 kHz crystal — the LED sits on 15);
- GPIO33–37 are the SPIIO4–7/SPIDQS lanes — **free on this board** because the in-package PSRAM is QSPI (they are only consumed by *octal* PSRAM variants);
- GPIO19/20 = USB D-/D+ — do not repurpose; GPIO0 = BOOT button (mind its power-on level — it decides boot mode).

## Caveats

- **No USB-Serial-JTAG (biggest difference vs the C3/S3 family boards)**: the S2 only has
  USB-OTG. Console output uses the ROM CDC driver (`CONFIG_ESP_CONSOLE_USB_CDC=y`).
  Consequences:
  - ROM bootloader stage logs still go to **UART0 (GPIO43/44, not broken out)** — early boot
    messages are invisible on the USB port; you only see output once the app's CDC is up;
  - The ROM CDC console is **incompatible with the TinyUSB stack** (IDF Kconfig enforces this) — don't add `esp_tinyusb` based components that claim the USB device;
  - `idf.py monitor` works, but the port re-enumerates across resets (ROM CDC ↔ app CDC are two different USB devices) — expect a brief disconnect on every reboot/panic.
- **Entering download mode: no auto-download circuit.** Hold **BOOT** (GPIO0), tap **RESET**, release BOOT → ROM enumerates as a CDC device and esptool can flash. Plain `idf.py -p COMx flash` without that gesture will fail to sync. (serialtap's DTR/RTS auto-reset tricks from the C3/S3 boards don't apply — nothing on this board wires DTR/RTS to EN/IO0.)
- **PSRAM unverified on real hardware (2026-10-03)**: spec says 2 MB QSPI in-package; `sdkconfig.defaults` enables `CONFIG_SPIRAM=y` + QUAD @ 80 MHz. If a unit boot-loops right at PSRAM init, first try `SPIRAM_SPEED_40M`, then `CONFIG_SPIRAM=n` — same triage as the esp32-s3-zero Octal-PSRAM lesson (a PSRAM init hang makes USB vanish; power-cycle + BOOT into download mode recovers).
- **GPIO11–20 = ADC2**: readings are garbage while WiFi is on (chip-level constraint, not board wiring). Use ADC1 (GPIO1–10) in WiFi projects.
- The schematic chip label reads `ESP32-S2FH4` (no PSRAM) while Wemos' product page says `ESP32-S2FN4R2` — the schematic likely predates a chip swap. Check the bootloader/esptool line (`Embedded PSRAM 2MB` or none) on the real unit before relying on PSRAM.
- D1 mini shield compatibility is this board's differentiator — but older shields assume 5 V logic; this board is 3.3 V.

## Serialtap (middleware) integration points

- The board enumerates as a plain USB CDC-ACM (no JTAG flavor) — serialtap discovery/naming/capture/passthrough works as-is; flashing through the proxy needs the manual BOOT+RESET gesture (above), there is no serial-level auto-reset to automate;
- 4 MB flash with the family dual-OTA layout — flashing time is in the same order as the C3 boards;
- The mono LED (GPIO15) can serve as the device status LED; serialtap/homepulse can push on/off through the proxy (board-side project work).

## Project Index

| Project | Description |
|---------|-------------|
| [blink](blink/README.md) | Baseline/test firmware: LED (GPIO15) blink + BOOT interaction + heartbeat logging + web maintenance page (provisioning/OTA) + TWDT watchdog |
