#include "scan_store.h"

#include <Arduino.h>
#include <strings.h>

#include "config.h"

namespace store {
namespace {

SemaphoreHandle_t s_mutex = nullptr;

WifiEntry s_wifi[S3_DEFENDER_WIFI_MAX];
uint16_t  s_wifiN = 0;
BleEntry  s_ble[S3_DEFENDER_BLE_MAX];
uint16_t  s_bleN = 0;

volatile uint32_t s_wifiRev = 0;
volatile uint32_t s_bleRev = 0;
volatile uint32_t s_newEvents = 0; // count of genuinely new device sightings

// Unsigned subtraction is wrap-safe for uptime deltas (< 49.7 days).
inline uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }

// rssi is never 0, so 0 is a safe "never logged" marker.
inline bool rssiMoved(int8_t last, int8_t cur) {
  return last == 0 || abs(cur - last) >= S3_DEFENDER_LOG_RSSI_DELTA;
}

// Decide whether this sighting warrants a CSV row and record the decision.
bool shouldLog(WifiMeta& m, uint32_t nowMs) {
  bool go = false;
  if (elapsed(nowMs, m.lastLoggedMs) >= S3_DEFENDER_LOG_REFIRE_MS) go = true;
  if (m.lastLoggedRssi == 0 || rssiMoved(m.lastLoggedRssi, 0 /*set below*/)) go = false; // placeholder
  (void)go;
  return false; // replaced below
}

} // namespace

bool begin() {
  if (s_mutex) return true;
  s_mutex = xSemaphoreCreateMutex();
  return s_mutex != nullptr;
}

Upsert upsertWifi(const wifiscan::Record& r, uint32_t nowMs) {
  Upsert out{false, false};
  if (!s_mutex) return out;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  int found = -1;
  for (uint16_t i = 0; i < s_wifiN && found < 0; ++i)
    if (memcmp(s_wifi[i].r.bssid, r.bssid, 6) == 0) found = i;
  uint16_t slot;
  bool wasNew = false;
  if (found >= 0) {
    slot = (uint16_t)found;
  } else if (s_wifiN < S3_DEFENDER_WIFI_MAX) {
    slot = s_wifiN++;
    wasNew = true;
  } else {
    // store full: oldest-sighted entry moves to the last slot and is replaced
    uint16_t old = 0;
    for (uint16_t i = 1; i < s_wifiN; ++i)
      if ((int32_t)(s_wifi[i].m.lastSeenMs - s_wifi[old].m.lastSeenMs) < 0) old = i;
    if (old != s_wifiN - 1) s_wifi[old] = s_wifi[s_wifiN - 1];
    slot = s_wifiN - 1;
    wasNew = true;
  }
  WifiEntry& e = s_wifi[slot];
  e.r = r;
  if (wasNew) {
    e.m.firstSeenMs = nowMs;
    e.m.count = 1;
    e.m.lastLoggedMs = 0;
    e.m.lastLoggedRssi = 0;
    s_newEvents++;
  } else {
    ++e.m.count;
  }
  e.m.lastSeenMs = nowMs;
  s_wifiRev++;

  const bool firstEverLog = e.m.lastLoggedMs == 0;
  const bool aged = elapsed(nowMs, e.m.lastLoggedMs) >= S3_DEFENDER_LOG_REFIRE_MS;
  const bool moved = rssiMoved(e.m.lastLoggedRssi, r.rssi);
  out.storeNew = wasNew;
  out.log = firstEverLog || aged || moved;
  if (out.log) {
    e.m.lastLoggedMs = nowMs;
    e.m.lastLoggedRssi = r.rssi;
  }
  xSemaphoreGive(s_mutex);
  return out;
}

Upsert upsertBle(const blescan::Record& r, uint32_t nowMs) {
  Upsert out{false, false};
  if (!s_mutex) return out;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  int found = -1;
  for (uint16_t i = 0; i < s_bleN && found < 0; ++i)
    if (s_ble[i].r.addrType == r.addrType &&
        memcmp(s_ble[i].r.addr, r.addr, 6) == 0) found = i;
  uint16_t slot;
  bool wasNew = false;
  if (found >= 0) {
    slot = (uint16_t)found;
  } else if (s_bleN < S3_DEFENDER_BLE_MAX) {
    slot = s_bleN++;
    wasNew = true;
  } else {
    uint16_t old = 0;
    for (uint16_t i = 1; i < s_bleN; ++i)
      if ((int32_t)(s_ble[i].m.lastSeenMs - s_ble[old].m.lastSeenMs) < 0) old = i;
    if (old != s_bleN - 1) s_ble[old] = s_ble[s_bleN - 1];
    slot = s_bleN - 1;
    wasNew = true;
  }
  BleEntry& e = s_ble[slot];
  // a later-arriving name fills an empty name, never erases a known one
  if (wasNew || e.r.name[0] == 0 || r.name[0]) {
    memcpy(e.r.name, r.name, sizeof(r.name));
  }
  memcpy(e.r.addr, r.addr, 6);
  e.r.addrType = r.addrType;
  e.r.rssi = r.rssi;
  e.r.advLen = r.advLen;
  e.r.connectable = r.connectable;
  if (wasNew) {
    e.m.firstSeenMs = nowMs;
    e.m.count = 1;
    e.m.lastLoggedMs = 0;
    e.m.lastLoggedRssi = 0;
    s_newEvents++;
  } else {
    ++e.m.count;
  }
  e.m.lastSeenMs = nowMs;
  s_bleRev++;

  const bool firstEverLog = e.m.lastLoggedMs == 0;
  const bool aged = elapsed(nowMs, e.m.lastLoggedMs) >= S3_DEFENDER_LOG_REFIRE_MS;
  const bool moved = rssiMoved(e.m.lastLoggedRssi, r.rssi);
  out.storeNew = wasNew;
  out.log = firstEverLog || aged || moved;
  if (out.log) {
    e.m.lastLoggedMs = nowMs;
    e.m.lastLoggedRssi = r.rssi;
  }
  xSemaphoreGive(s_mutex);
  return out;
}

uint16_t wifiCount() {
  if (!s_mutex) return 0;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  uint16_t n = s_wifiN;
  xSemaphoreGive(s_mutex);
  return n;
}

uint16_t bleCount() {
  if (!s_mutex) return 0;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  uint16_t n = s_bleN;
  xSemaphoreGive(s_mutex);
  return n;
}

uint16_t copyWifi(WifiEntry* out, uint16_t max, Sort s) {
  if (!s_mutex || !out || !max) return 0;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  uint16_t n = s_wifiN;
  if (n > max) n = max;
  memcpy(out, s_wifi, n * sizeof(WifiEntry));
  xSemaphoreGive(s_mutex);
  switch (s) {
    case Sort::ChannelAsc:
      qsort(out, n, sizeof(WifiEntry), [](const void* a, const void* b) -> int {
        return (int)((const WifiEntry*)a)->r.channel -
               (int)((const WifiEntry*)b)->r.channel;
      });
      break;
    case Sort::NameAsc:
      // empty SSIDs sort to the end
      qsort(out, n, sizeof(WifiEntry), [](const void* a, const void* b) -> int {
        auto* f = (const WifiEntry*)a;
        auto* g = (const WifiEntry*)b;
        if (!f->r.ssid[0]) return 1;
        if (!g->r.ssid[0]) return -1;
        return strcasecmp(f->r.ssid, g->r.ssid);
      });
      break;
    case Sort::LastSeenDesc:
      qsort(out, n, sizeof(WifiEntry), [](const void* a, const void* b) -> int {
        return (int32_t)((const WifiEntry*)b)->m.lastSeenMs -
               (int32_t)((const WifiEntry*)a)->m.lastSeenMs;
      });
      break;
    default:
      qsort(out, n, sizeof(WifiEntry), [](const void* a, const void* b) -> int {
        return (int)((const WifiEntry*)b)->r.rssi - (int)((const WifiEntry*)a)->r.rssi;
      });
      break;
  }
  return n;
}

uint16_t copyBle(BleEntry* out, uint16_t max, Sort s) {
  if (!s_mutex || !out || !max) return 0;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  uint16_t n = s_bleN;
  if (n > max) n = max;
  memcpy(out, s_ble, n * sizeof(BleEntry));
  xSemaphoreGive(s_mutex);
  switch (s) {
    case Sort::NameAsc:
      qsort(out, n, sizeof(BleEntry), [](const void* a, const void* b) -> int {
        auto* f = (const BleEntry*)a;
        auto* g = (const BleEntry*)b;
        if (!f->r.name[0]) return 1;
        if (!g->r.name[0]) return -1;
        return strcasecmp(f->r.name, g->r.name);
      });
      break;
    case Sort::LastSeenDesc:
      qsort(out, n, sizeof(BleEntry), [](const void* a, const void* b) -> int {
        return (int32_t)((const BleEntry*)b)->m.lastSeenMs -
               (int32_t)((const BleEntry*)a)->m.lastSeenMs;
      });
      break;
    default: // RssiDesc / ChannelAsc (BLE has no channel)
      qsort(out, n, sizeof(BleEntry), [](const void* a, const void* b) -> int {
        return (int)((const BleEntry*)b)->r.rssi - (int)((const BleEntry*)a)->r.rssi;
      });
      break;
  }
  return n;
}

void prune(uint32_t nowMs) {
  if (!s_mutex) return;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  uint16_t w = 0;
  for (uint16_t i = 0; i < s_wifiN; ++i) {
    if (elapsed(nowMs, s_wifi[i].m.lastSeenMs) > S3_DEFENDER_WIFI_TTL_MS) continue;
    if (w != i) s_wifi[w] = s_wifi[i];
    ++w;
  }
  if (w != s_wifiN) s_wifiRev++;
  s_wifiN = w;
  uint16_t b = 0;
  for (uint16_t i = 0; i < s_bleN; ++i) {
    if (elapsed(nowMs, s_ble[i].m.lastSeenMs) > S3_DEFENDER_BLE_TTL_MS) continue;
    if (b != i) s_ble[b] = s_ble[i];
    ++b;
  }
  if (b != s_bleN) s_bleRev++;
  s_bleN = b;
  xSemaphoreGive(s_mutex);
}

void clear() {
  if (!s_mutex) return;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_wifiN = 0;
  s_bleN = 0;
  s_wifiRev++;
  s_bleRev++;
  xSemaphoreGive(s_mutex);
}

uint32_t wifiRev() { return s_wifiRev; }
uint32_t bleRev() { return s_bleRev; }
uint32_t newEvents() { return s_newEvents; }

} // namespace store