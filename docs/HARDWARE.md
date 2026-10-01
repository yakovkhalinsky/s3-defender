# Hardware — LilyGO T-Dongle-S3

Official repo: [Xinyuan-LilyGO/T-Dongle-S3](https://github.com/Xinyuan-LilyGO/T-Dongle-S3)  
Docs: [wiki.lilygo.cc — T-Dongle-S3](https://wiki.lilygo.cc/products/t-dongle-series/t-dongle-s3/)

## Specs

- ESP32-S3, Wi‑Fi 802.11 b/g/n + Bluetooth 5 LE  
- 16 MB flash, **no PSRAM**  
- 0.96″ ST7735 SPI TFT, 160 × 80  
- APA102 RGB LED (BGR colour order)  
- TF card (SDMMC)  
- BOOT on GPIO0  
- USB Type‑A plug (CDC + JTAG)

## GPIO map

| Name | GPIO |
|---|---|
| TFT CS | 4 |
| TFT MOSI (SDA) | 3 |
| TFT SCLK (SCL) | 5 |
| TFT DC | 2 |
| TFT RST | 1 |
| TFT BL | 38 |
| APA102 DIN | 40 |
| APA102 CLK | 39 |
| BOOT | 0 |
| SDMMC D0 | 14 |
| SDMMC D1 | 17 |
| SDMMC D2 | 21 |
| SDMMC D3 | 18 |
| SDMMC CLK | 12 |
| SDMMC CMD | 16 |

## PlatformIO

See root `platformio.ini` (`env:t-dongle-s3`). Highlights:

- **`USE_HSPI_PORT=1` is mandatory** for TFT_eSPI on arduino core ≥ 2.0.15:
  the core's `REG_SPI_BASE(i)` returns NULL for SPI port ids < 2 on the S3, and
  TFT_eSPI's default FSPI port then panics (`StoreProhibited`) at `tft.init()`.
  Using the HSPI/GPSPI3 port avoids it (same as LilyGO's own
  `Setup47_ST7735.h`).
- Display flags (copied from LilyGO's validated setup): `ST7735_GREENTAB160x80`,
  `TFT_WIDTH=80`, `TFT_HEIGHT=160`, `TFT_RGB_ORDER=TFT_BGR`,
  `SPI_FREQUENCY=27000000`. Wrong colors/geometry on your unit? See the
  fallback list in README's bring-up checklist.
- Backlight (GPIO38) is owned by the app (LEDC PWM), not the library.
- SD: `SD_MMC.setPins(12, 16, 14, 17, 21, 18)` + `SD_MMC.begin("/sdcard",
  false, false, SDMMC_FREQ_DEFAULT, 5)` — 4-bit is what LilyGO's own
  `sd_card` example uses; define `S3_DEFENDER_SD_1BIT` for a 1-bit fallback.
- APA102: payload byte order is **B, G, R**; if red renders blue, define
  `S3_DEFENDER_APA102_RGB`.

## Arduino IDE (reference)

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| USB Mode | Hardware CDC and JTAG |
| CPU Frequency | 240 MHz (WiFi) |
| Flash Mode | QIO 80 MHz |
| Flash Size | 16 MB |
| Partition Scheme | 16M Flash (3MB APP / 9.9MB FATFS) or similar |
| PSRAM | **Disabled** |
| Upload Speed | 921600 |

## Flashing tips

1. Prefer a direct USB‑A port (or a known-good USB‑A hub).  
2. If the port does not appear: hold **BOOT**, plug in, release after the tool starts upload.  
3. After a successful flash, unplug and replug **without** holding BOOT.
