# blink — Baseline (Test Firmware)

> Isomorphic copy of [`esp32-c3-mini/blink`](../../esp32-c3-mini/blink) (copy-first convention).

## What it does

- Onboard **LED (GPIO15, active-high)** toggles every second;
- **BOOT button (GPIO0)** held keeps the LED solid on (interaction self-check); release resumes blinking;
- One heartbeat log line every 10 s (`uptime` / `heap`) for serialtap capture;
- Onboard **web maintenance page :80**: WiFi provisioning / firmware OTA / reboot
  (rescue SoftAP `blink-s2m` / `12345678` → `192.168.4.1` when unprovisioned);
- **Watchdog**: ESP-IDF TWDT, 5 s timeout with panic (main loop feeds every 1 s);
- **OTA**: dual OTA slots (app at `0x20000`); the web page streams to the
  alternate slot → verify → switch → reboot.

## Build & Flash

```bash
cd blink
idf.py set-target esp32s2
idf.py build
# Enter download mode first: hold BOOT, tap RESET, release BOOT
idf.py -p COMx flash monitor
```

- Console runs on the S2's native **USB-OTG ROM CDC** (no USB-Serial-JTAG, no bridge chip);
  the port re-enumerates across resets and ROM-stage boot logs are not visible on USB;
- Flashing requires the manual **BOOT + RESET** gesture — there is no auto-download circuit;
- Release assets include `flash_blink.sh/.bat` (bootloader + partition table + app in one go).

## Firmware baseline norms

| Baseline | Status |
|----------|--------|
| Watchdog | ✅ TWDT 5 s panic, main loop feeds |
| Web/API OTA | ✅ dual OTA slots + streaming `POST /ota` |
