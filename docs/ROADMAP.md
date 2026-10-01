# Roadmap

## Phase 0 — scaffold (this repo)

- [x] README, LICENSE, `.gitignore`
- [x] Hardware / architecture / roadmap docs
- [x] PlatformIO env for T-Dongle-S3
- [x] SoftAP stub + placeholder HTML
- [x] Serial banner + TODO hooks

## Phase 1 — SoftAP UI shell

- [ ] Serve embedded or LittleFS UI (list + mode toggle)
- [ ] `/api/status` JSON
- [ ] BOOT cycles mode (stubbed scanners still OK)

## Phase 2 — live radio

- [ ] Wi‑Fi scan / promiscuous beacons → `scan_store`
- [ ] BLE scan → `scan_store`
- [ ] WebSocket push of sorted lists (RSSI)

## Phase 3 — on-device UX

- [ ] ST7735: mode, counts, top Wi‑Fi / BLE line
- [ ] APA102 activity colour
- [ ] Simple sleep / dim after idle

## Phase 4 — logging

- [ ] microSD CSV (timestamp, type, id, rssi)
- [ ] Download last log over HTTP

## Phase 5 — PWA

- [ ] Manifest + service worker (offline shell)
- [ ] “Add to Home Screen” friendly layout for phones

## Non-goals

- Deauthentication / jamming  
- Credential capture or handshake cracking  
- Hidden packet injection gadgets
