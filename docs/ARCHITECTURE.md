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
| `wifi_scan` | Standard station scan (`WiFi.scanNetworks` async) every ~6 s; internal channel hop + restore keeps the SoftAP reachable; adaptive dwell (100 ms/ch, 60 ms while a web client is mounted) |
| `ble_scan` | NimBLE scanner-only: active scan, 50% duty (100/50 ms), duplicate filter, **callback-only** (`setMaxResults(0)` — nothing accumulates in the stack); 2 s cycles |
| `scan_store` | Bounded static tables (48 Wi-Fi / 64 BLE entries) + meta; single mutex; upsert by BSSID / addr+type; prune at 1 Hz |
| `display` | ST7735 via TFT_eSPI (landscape 160×80): header, top-3 Wi-Fi, top-3 BLE, footer; per-line cached redraw; LEDC backlight On/Dim/Sleep |
| `led` | APA102 bit-bang (BGR): idle cyan breathing, white flash on a *new* device, blue busy, red on SD error / low heap |
| `web` | AsyncWebServer 80: embedded UI, `/ws` 1 Hz full-snapshot push, `/api/status`, `/api/list`, `/api/mode`, `/api/display`, `/api/time`, `/api/logs`, `/download`, PWA manifest/icons |
| `storage` | SD_MMC (4-bit, 20 MHz): CSV appends only from its task; flush + 8 MB rotation; producers enqueue fixed rows |
| `input` | BOOT: short press cycles mode; long press toggles display dim/sleep |
| `app` | Mode owner (atomic) + NVS persistence + epoch (`/api/time`) |
| `main` | Thin setup/loop + the 10 Hz `ui` task |

## Tasks, cores, priorities

| Task | Core | Prio | Stack | Notes |
|---|---|---|---|---|
| Arduino `loopTask` | 1 | 1 | 8 KB | mode owner, NVS debounce |
| `wifi_scan` | 1 | 2 | 6 KB | scan cycles; radio work happens via IDF on core 0 |
| `ble_scan` | 1 | 2 | 4 KB | scanner loop; callbacks arrive on the NimBLE host task |
| `ui` | 1 | 1 | 4 KB | 10 Hz: snapshot, display, LED, prune, web tick |
| `storage` | 1 | 1 | 4 KB | the only task that writes the card |
| `async_tcp` | 1 | 10 | 16 KB | HTTP + WS (pinned via `CONFIG_ASYNC_TCP_RUNNING_CORE`) |
| `nimble_host`, WiFi/lwIP | 0 | — | — | stacks stay off our cores |

One shared mutex (`scan_store`) held only for memcpy. `mode` is an atomic written by `loopTask`; download state is an atomic pair between web and storage.

## Radio / SoftAP behavior

One radio serves both the AP and the scans:

- Wi-Fi scan is *not* always-promiscuous: a hop sniffer would leave the AP's channel and orphan connected clients. Instead the scan API is used (hops internally, restores). Consequence: for ~1–2 s every scan cycle the AP is hard to reach — web clients see the WS pause but survive. Escape hatch: `S3_DEFENDER_SCAN_PAUSE_WITH_CLIENTS`.
- This scan sends probe requests like any phone; the README says so. A strict-passive promiscuous mode (used only while zero web clients are attached) is the natural future extension.
- BLE runs at 50% duty cycle so the coexistence scheduler can interleave AP traffic.
- The AP channel is compiled in (`S3_DEFENDER_SOFTAP_CHANNEL=1`), so it always returns to a known channel.

## Memory budget (no PSRAM — ~320 KB usable)

| Consumer | RAM |
|---|---|
| NimBLE host scan-only | ~40 KB heap |
| SoftAP + lwIP | ~50–60 KB heap |
| scan_store tables (static) | ~6 KB |
| Web scratch (json buffer + entries) | ~10 KB (static) |
| AsyncWS 2 clients × 8 KB frames | 15–25 KB |
| CSV task + queue | ~5 KB |
| **Expected free after init** | **~120–170 KB** (red LED under 32 KB) |

Guardrails: no `String` in hot paths; no heap allocation in radio callbacks; `WiFi.scanDelete()` every cycle; `setMaxResults(0)`; 1 WS frame/s ≤ 2 clients; snapshot auto-shrinks (24→12→8 rows) to stay under `S3_DEFENDER_JSON_MAX_BYTES`.

## Data model (web JSON sketch)

```json
{
  "name": "s3-defender", "ver": "0.1.0",
  "mode": "both", "uptime_s": 120, "epoch_s": 0,
  "heap": {"free": 152123, "min": 148000, "frag_pct": 3},
  "scan": {"wifi": "idle", "ble": "scanning", "next_s": 3},
  "wifi_count": 37, "ble_count": 12,
  "wifi": [
    {"ssid": "Home", "bssid": "aa:bb:..", "rssi": -51, "ch": 6,
     "auth": "wpa2", "seen_ms": 40, "first_ms": 120, "n": 9}
  ],
  "ble": [
    {"name": "AirPods", "addr": "11:22:..", "at": 1, "rssi": -62,
     "conn": true, "adv_len": 22, "seen_ms": 12, "first_ms": 3, "n": 4}
  ],
  "sd": {"mounted": true, "used_bytes": 48211, "card_bytes": 32768673792},
  "ws": {"clients": 1, "topk": 24}
}
```

- `ssid == ""` → hidden network; BLE `name == ""` → no name advertised.
- `seen_ms`/`first_ms` are **ages** (ms since last/first sighting), not timestamps.
- WS frames carry the top-K rows; totals in `wifi_count`/`ble_count` signal "more available" → client fetches `/api/list`.