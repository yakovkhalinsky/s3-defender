# Roadmap

## Bring-up still to verify (on-device, live)

Board flashed + web UI + both radios + display verified 2026-10-01/02.

- [ ] Click `⬇ download log.csv` on the dashboard and confirm the CSV opens with sane rows
- [ ] 24 h soak: `beat` heartbeat heap stable, CSV grows at a plausible rate, no watchdog reset
- [ ] Card-hotplug: pull the card while logging, reinsert, confirm the 60 s re-mount retry works
- [ ] Decide: trim the per-window `ble:` serial print once things stay stable (field-debug value vs chatter)

## Phase 0 — scaffold

- [x] README, LICENSE, `.gitignore`
- [x] Hardware / architecture / roadmap docs
- [x] PlatformIO env for T-Dongle-S3

- [x] README, LICENSE, `.gitignore`
- [x] Hardware / architecture / roadmap docs
- [x] PlatformIO env for T-Dongle-S3

## Phase 1 — SoftAP UI shell

- [x] Embedded UI (generated header from `data/index.html`)
- [x] `/api/status` JSON + mode endpoints (`/api/mode`, `/api/display`, `/api/time`)
- [x] BOOT cycles mode; long press toggles screen dim/sleep

## Phase 2 — live radio

- [x] Wi‑Fi scan (standard scan API, not promiscuous) → `scan_store`
- [x] BLE scan (NimBLE, scan-only) → `scan_store`
- [x] WebSocket push of sorted lists (1 Hz full snapshot, adaptive top-K)

## Phase 3 — on-device UX

- [x] ST7735: mode, counts, top Wi‑Fi / BLE rows
- [x] APA102 activity color (new-device flash, idle breathing)
- [x] Dim after 60 s idle; long-press sleep toggle

## Phase 4 — logging

- [x] microSD CSV (epoch/uptime, kind, id, name, rssi, ch, first, mode)
- [x] Streaming log download over HTTP + 8 MB rotation

## Phase 5 — PWA (no service worker)

- [x] Manifest + generated icons + iOS/Android Add-to-Home-Screen meta
- [x] Amended: a service worker is impossible on plain `http://192.168.4.1`
      (insecure origin) — no offline shell; manifest-only install instead

## Open extensions (not built)

- [ ] Promiscuous Wi‑Fi observation, auto-enabled only while no web client is
      attached (strict-passive; would avoid the on/off-channel scan disruption)
- [ ] IRK resolution to group a phone's rotating random addresses (RPA)

## Non-goals

- Deauthentication / jamming
- Credential capture or handshake cracking
- Hidden packet injection gadgets
