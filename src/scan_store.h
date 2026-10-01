#pragma once
// scan_store — the single shared mutable state: bounded, mutex-guarded tables
// of recently observed Wi-Fi APs and BLE advertisers.
//
// Everything is fixed-size (no PSRAM): upserts update in place or evict the
// oldest-sighted entry; a 1 Hz prune() ages entries out. Producers call
// upsert* from radio callbacks/tasks; consumers take bounded snapshots via
// copyWifi/copyBle into caller-owned memory (never any work while the lock is
// held beyond a memcpy).
//
// Logging policy is decided here (30 s refire / 5 dB delta / first sighting)
// because the per-device bookkeeping already lives in the slots; if the SD is
// not mounted the producers simply skip queueing.
#include <cstdint>
#include <cstddef>

#include "wifi_scan.h"
#include "ble_scan.h"

namespace store {

struct WifiMeta {
  uint32_t lastSeenMs; // uptime ms of last sighting
  uint32_t firstSeenMs;
  uint32_t lastLoggedMs;
  uint16_t count;      // sightings this session
  int8_t   lastLoggedRssi;
};

struct BleMeta {
  uint32_t lastSeenMs;
  uint32_t firstSeenMs;
  uint32_t lastLoggedMs;
  uint16_t count;
  int8_t   lastLoggedRssi;
};

struct WifiEntry { wifiscan::Record r; WifiMeta m; };
struct BleEntry  { blescan::Record r; BleMeta m; };

enum class Sort : uint8_t { RssiDesc, ChannelAsc, NameAsc, LastSeenDesc };

struct Upsert { bool storeNew; bool log; };

bool     begin();
Upsert   upsertWifi(const wifiscan::Record& r, uint32_t nowMs); // wifi task only
Upsert   upsertBle(const blescan::Record& r, uint32_t nowMs);   // NimBLE host task only
uint16_t wifiCount();
uint16_t bleCount();
uint16_t copyWifi(WifiEntry* out, uint16_t max, Sort s);
uint16_t copyBle(BleEntry* out, uint16_t max, Sort s);
void     prune(uint32_t nowMs); // 1 Hz from the ui task
void     clear();
uint32_t wifiRev();  // bumps on any wifi-table mutation
uint32_t bleRev();   // bumps on any ble-table mutation
uint32_t newEvents(); // bumps when a genuinely NEW device (per kind) appears

// CSV sanity (bounded static RAM): keep entries compact.
static_assert(sizeof(WifiEntry) < 64, "wifi entry too large");
static_assert(sizeof(BleEntry) < 64, "ble entry too large");

} // namespace store