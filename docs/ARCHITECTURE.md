# Architecture

s3-defender splits into small firmware modules and a browser UI served from the ESP32.

```
┌─────────────────────────────────────────────────────────┐
│                    T-Dongle-S3 firmware                 │
│  ┌──────────┐  ┌──────────┐  ┌─────────┐  ┌─────────┐ │
│  │ wifi_scan│  │ ble_scan │  │ display │  │   led   │ │
│  └────┬─────┘  └────┬─────┘  └────┬────┘  └────┬────┘ │
│       │             │             │            │      │
│       └──────┬──────┴──────┬──────┘            │      │
│              ▼             ▼                   ▼      │
│         ┌─────────────────────┐         activity RGB  │
│         │   scan_store (RAM)  │                       │
│         └──────────┬──────────┘                       │
│                    ▼                                  │
│         ┌─────────────────────┐     ┌──────────────┐  │
│         │  web (SoftAP HTTP   │◄───►│ storage (TF) │  │
│         │  + WebSocket JSON)  │     │   optional   │  │
│         └─────────────────────┘     └──────────────┘  │
└─────────────────────────────────────────────────────────┘
                          ▲
                          │ Wi‑Fi SoftAP
                          ▼
                   phone / laptop browser
```

## Modules

| Module | Role |
|---|---|
| `wifi_scan` | Channel hop + beacon/probe observation; emit SSID, BSSID, RSSI, channel, last-seen |
| `ble_scan` | Active/passive BLE scan; emit name (if any), address, RSSI, ADV type |
| `scan_store` | In-RAM ring / map of recent devices; eviction by age |
| `display` | ST7735 status: mode, Wi‑Fi count, BLE count, strongest hit |
| `led` | APA102 pulse on new sightings |
| `web` | SoftAP, static UI, `/api/status`, WebSocket `/ws` for live lists |
| `storage` | Optional microSD CSV append (phase later) |
| `input` | BOOT button → cycle Wi‑Fi / BLE / both |

## Data model (web JSON sketch)

```json
{
  "mode": "both",
  "uptime_s": 120,
  "wifi": [
    {"ssid": "Home", "bssid": "aa:bb:..", "rssi": -51, "ch": 6, "seen_ms": 40}
  ],
  "ble": [
    {"name": "AirPods", "addr": "11:22:..", "rssi": -62, "seen_ms": 12}
  ]
}
```

## Concurrency

ESP32-S3 dual core: keep radio callbacks light; push into a FreeRTOS queue; one task owns `scan_store` and fans out to display + WebSocket clients.
