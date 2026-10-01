# s3-defender

Passive **Wi‑Fi + BLE** air monitor for the [LilyGO T-Dongle-S3](https://github.com/Xinyuan-LilyGO/T-Dongle-S3) — a USB‑A stick with a tiny colour screen.

Plug it in, join SoftAP **`S3-Defender`**, open the web UI, and watch nearby networks and BLE advertisements. On-device TFT shows live counts; the APA102 LED blinks with activity.

> **Passive only.** This project observes beacons and advertisements (SSID / BSSID / RSSI / channel, BLE ADV). It does **not** deauth, crack, inject, or MitM. Use it on networks and devices you are allowed to monitor.

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
| SoftAP + placeholder web page | ✅ stub |
| Wi‑Fi promiscuous / scan | ⏳ planned |
| BLE scan | ⏳ planned |
| Live WebSocket JSON UI | ⏳ planned |
| TFT + APA102 UX | ⏳ planned |
| TF CSV logging | ⏳ planned |
| Installable PWA | ⏳ later |

See [docs/ROADMAP.md](docs/ROADMAP.md).

## Quick start (PlatformIO)

1. Install [VS Code](https://code.visualstudio.com/) + [PlatformIO](https://platformio.org/).
2. Clone this repo and open the folder in VS Code.
3. Plug the T-Dongle-S3 into USB‑A.
4. Build & upload (`PlatformIO: Upload`). If upload fails, hold **BOOT** while plugging in, upload, then unplug/replug without BOOT.
5. Open serial monitor at **115200**. SoftAP should advertise **`S3-Defender`** (open / default password in firmware when implemented).
6. Connect a phone or laptop to that AP and open the URL printed on serial (typically `http://192.168.4.1`).

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
