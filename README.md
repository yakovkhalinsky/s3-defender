# s3-defender

Passive **Wi‑Fi + BLE** air monitor for the [LilyGO T-Dongle-S3](https://github.com/Xinyuan-LilyGO/T-Dongle-S3) — a USB‑A stick with a tiny colour screen.

Plug it in, join SoftAP **`S3-Defender`**, open the web UI, and watch nearby networks and BLE advertisements. On-device TFT shows live counts; the APA102 LED blinks with activity.

> **Passive only.** This project observes APs and advertisements (SSID / BSSID / RSSI / channel, BLE ADV). It does **not** deauth, crack, inject, or MitM. Use it on networks and devices you are allowed to monitor.
>
> Honesty note: Wi‑Fi discovery uses the standard station scan (the same probe requests any phone sends), because the radio must keep serving the SoftAP/web UI. A strict‑passive promiscuous sniffer (only while no web client is attached) is a possible future toggle — see ROADMAP "Open extensions".

### Install (browser)

On plain `http://192.168.4.1` a service worker can't run (insecure origin — that's a browser rule, not a config). What works:

- **iOS Safari**: Share → *Add to Home Screen* → opens standalone (full‑screen, your icon).
- **Android Chrome**: menu → *Add to Home screen* (browser‑badged shortcut; the automatic install prompt needs HTTPS).

## Libraries (all pinned)

| | |
|---|---|
| platform | `espressif32 @ 6.12.0` (arduino core 2.0.17, IDF 4.4.7) |
| BLE | `NimBLE-Arduino @ 1.4.3` (2.x needs core 3.x) |
| Web | `ESPAsyncWebServer @ 3.12.1` + `AsyncTCP @ 3.5.0` (ESP32Async forks) |
| Display | `TFT_eSPI @ 2.5.43` (configured in `platformio.ini`) |
| JSON | `ArduinoJson @ 7.4.3` |

## Hardware

| | |
|---|---|
| Board | LilyGO **T-Dongle-S3** |
| SoC | ESP32-S3 (Wi‑Fi + BLE 5) |
| Display | 0.96″ ST7735, **160 × 80**, SPI |
| LED | APA102 (BGR) |
| Storage | microSD (TF) slot |
| Input | BOOT button |
| Flash | 16 MB (no PSRAM) |

## Status

| Area | State |
|---|---|
| Docs + PlatformIO skeleton | ✅ |
| SoftAP + web UI (live lists, mode) | ✅ |
| Wi‑Fi scan → live lists | ✅ (standard scan, not promiscuous) |
| BLE scan (NimBLE) | ✅ |
| Live WebSocket JSON UI | ✅ (1 Hz full snapshot) |
| TFT + APA102 UX | ✅ (colors need one on‑device check) |
| TF CSV logging + download | ✅ |
| Installable PWA | ✅ manifest/icons (no service worker — plain‑IP HTTP can't run one) |

See [docs/ROADMAP.md](docs/ROADMAP.md).

## Quick start (PlatformIO)

1. Install [VS Code](https://code.visualstudio.com/) + [PlatformIO](https://platformio.org/).
2. Clone this repo and open the folder in VS Code.
3. Plug the T-Dongle-S3 into USB‑A.
4. Build & upload (`PlatformIO: Upload`). If upload fails, hold **BOOT** while plugging in, upload, then unplug/replug without BOOT.
5. Open serial monitor at **115200**. SoftAP should advertise **`S3-Defender`** (open; define `S3_DEFENDER_SOFTAP_PASS` in `platformio.ini` for WPA2).
6. Connect a phone or laptop to that AP and open the URL printed on serial (`http://192.168.4.1`).

## Bring-up checklist (first flash)

1. **Serial** (115200): banner, then a heartbeat every 5 s (`beat … heap= w= b= mode= sta= ap=`) — a running app is always visible on serial. Serial commands (type + Enter): `ap` (AP state), `scanq` (scan state), `bl N` (backlight), `rot N`, `bars` / `barsq` (test bars on/off), `inv` (invert colors).
2. **Web UI**: join `S3-Defender`, open `http://192.168.4.1` — live dot goes green when the WebSocket is up.
3. **Scan cycles**: the AP drops off-channel for ~1.5 s every 6 s; the dot should stay green. If your phone drops → define `S3_DEFENDER_SCAN_PAUSE_WITH_CLIENTS` and flash again.
4. **BOOT button**: short press cycles the scan mode (persists across replugs); long press toggles screen dim/sleep.
5. **TFT**: `bars` + `bl 255` over serial shows RED | GREEN | BLUE | WHITE bands. If they're wrong: colors swapped → add `-D TFT_RGB_ORDER=TFT_RGB`; colors inverted → add `-D TFT_INVERSION_ON=1`; geometry off → try `ST7735_BLACKTAB`/`GREENTAB`/`REDTAB160x80` instead of `ST7735_GREENTAB160x80`; upside down → `-D S3_DEFENDER_TFT_ROTATION=3`. Backlight seems dead on this board if it never brightens → its polarity may be active-low; if `bl 0` brightens, we'll flip the duty.
6. **APA102**: red pulse under low heap means the byte order is right; if red shows blue add `-D S3_DEFENDER_APA102_RGB=1`.
7. **SD**: insert card → `sd: mounted` appears on serial and `⬇ download log.csv` in the UI footer; `sd:` lines report problems (fallback: `-D S3_DEFENDER_SD_1BIT=1`).
8. After step 5 passes, set `-D S3_DEFENDER_TFT_TEST=0` in `platformio.ini` so the boot bars go away, and flash once more.

Arduino IDE settings (if you prefer Arduino over PlatformIO) are in [docs/HARDWARE.md](docs/HARDWARE.md).

## SoftAP

| | |
|---|---|
| SSID | `S3-Defender` |
| Default URL | `http://192.168.4.1` |

## Pin map (T-Dongle-S3)

| Function | GPIO |
|---|---|
| TFT CS | 4 |
| TFT SDA (MOSI) | 3 |
| TFT SCL (SCLK) | 5 |
| TFT DC | 2 |
| TFT RST | 1 |
| TFT backlight | 38 |
| APA102 DIN | 40 |
| APA102 CLK | 39 |
| BOOT button | 0 |
| SDMMC D0 | 14 |
| SDMMC D1 | 17 |
| SDMMC D2 | 21 |
| SDMMC D3 | 18 |
| SDMMC CLK | 12 |
| SDMMC CMD | 16 |

Full notes: [docs/HARDWARE.md](docs/HARDWARE.md). Architecture: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## License

MIT — see [LICENSE](LICENSE).
