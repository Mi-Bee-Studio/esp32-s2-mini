# env-station — Environment Sensing Node (AHT20 + SR501 + WiFi CSI)

> Concept copied from [`esp32-s3-zero/env-station`](../../esp32-s3-zero/env-station)
> (copy-first convention); web maintenance page + OTA reuse the [blink](../blink/README.md) baseline code.

## What it does

- **AHT20 temp/humidity** (I2C 0x38): sampled every 5 s with CRC8 check;
- **SR501 PIR presence** (GPIO6): level change → log + status report (the module
  has its own hold-time/sensitivity pots; firmware just reads the level: high = motion);
- **WiFi CSI capture**: enabled on STA association, streams `#S1` telemetry
  (family contract, see below) + gateway ping stimulus (1 Hz — replies are a
  stable downlink CSI source), rate-limited to 20 Hz;
- Onboard **web maintenance page :80**: status (temp/RH/PIR/CSI counters) /
  WiFi provisioning / OTA / reboot (rescue SoftAP `env-s2m` / `12345678` → `192.168.4.1`);
- **Watchdog**: TWDT 5 s panic, main loop feeds every 500 ms (CSI stream task subscribes too);
- **OTA**: dual OTA slots (app at `0x20000`); the web page streams to the
  alternate slot → verify → switch → reboot.

## Wiring (real build, 2026-10-03)

| Peripheral | Wire | Board GPIO |
|------------|------|------------|
| SR501 PIR OUT | → | **GPIO6** (module VCC 5 V / GND) |
| AHT20 SDA | → | **GPIO33** |
| AHT20 SCL | → | **GPIO35** |

- AHT20 wiring order assumed "SDA, SCL" as spoken; if probing fails the firmware
  **auto-retries with swapped pins** and logs the effective order — trust the log
  and fix this doc accordingly;
- AHT20 breakout boards usually have onboard pull-ups (4.7–10 kΩ); internal GPIO
  pull-ups are enabled as a fallback (fine at 100 kHz on short wires);
- GPIO33–37 are free with QSPI PSRAM (this board) — only *octal* PSRAM consumes them.

## CSI `#S1` contract

Identical to [`luatos-esp32c3/air101-lcd/wifi-csi-sensing`](../../luatos-esp32c3/air101-lcd/wifi-csi-sensing/README.md)
(input of `homepulse/internal/sense/parser.go` — **never change one side alone**):

- Session header: `#S1-HELLO nsc=<total subcarriers> sel=<16 selected indices> rate=20`
- Data lines: `#S1 <seq> <t_ms> <rssi> <hex64>` (16 subcarriers × (I,Q) 1 byte each, hex)
- 16 subcarriers spread evenly over `[nsc/8, nsc*7/8]`, skipping edge nulls and DC;
- Stimulus: after getting an IP, ping the gateway at 1 Hz (ACK/replies are
  downlink unicast frames → stable CSI source).

## Build & Flash

```bash
cd env-station
idf.py set-target esp32s2
idf.py build
# Enter download mode first: hold BOOT, tap RESET, release BOOT
idf.py -p COMx flash monitor
```

- Console runs on the S2's native **USB-OTG ROM CDC** (no USB-Serial-JTAG, no bridge chip);
- Flashing requires the manual **BOOT + RESET** gesture (no auto-download circuit);
- WiFi provisioning: connect a phone to SoftAP `env-s2m`/`12345678` →
  `http://192.168.4.1`, or open the maintenance page at the STA IP directly.

## Firmware baseline norms

| Baseline | Status |
|----------|--------|
| Watchdog | ✅ TWDT 5 s panic, main loop feeds @500 ms + CSI task subscribed |
| Web/API OTA | ✅ dual OTA slots + streaming `POST /ota` |
