#include "app.h"

#include <Arduino.h>
#include <Preferences.h>
#include <atomic>
#include <WiFi.h>

#include "ble_scan.h"
#include "config.h"
#include "display.h"
#include "scan_store.h"
#include "wifi_scan.h"

namespace app {
namespace {

std::atomic<uint8_t> s_mode{(uint8_t)Mode::Both};
std::atomic<bool>    s_savePending{false};
std::atomic<uint32_t> s_saveAtMs{0};
std::atomic<uint32_t> s_epochBase{0}; // epoch at uptime 0 (0 = unset)
Preferences          s_prefs;
bool                 s_prefsOk = false;

inline uint32_t nowUptime() { return millis() / 1000; }

} // namespace

const char* modeNameFor(uint8_t m) {
  switch ((Mode)m) {
    case Mode::Wifi: return "wifi";
    case Mode::Ble: return "ble";
    default: return "both";
  }
}

bool parseMode(const char* s, uint8_t len, uint8_t* out) {
  if (!s) return false;
  if (len == 4 && memcmp(s, "wifi", 4) == 0) { *out = (uint8_t)Mode::Wifi; return true; }
  if (len == 3 && memcmp(s, "ble", 3) == 0) { *out = (uint8_t)Mode::Ble; return true; }
  if (len == 4 && memcmp(s, "both", 4) == 0) { *out = (uint8_t)Mode::Both; return true; }
  return false;
}

void applyMode(uint8_t m) {
  wifiscan::setEnabled(m != (uint8_t)Mode::Ble);
  blescan::setEnabled(m != (uint8_t)Mode::Wifi);
}

bool begin() {
  s_prefsOk = s_prefs.begin("s3def", false);
  uint8_t m = s_prefsOk ? s_prefs.getUChar("mode", (uint8_t)Mode::Both)
                        : (uint8_t)Mode::Both;
  if (m > 2) m = (uint8_t)Mode::Both;
  s_mode.store(m);
  applyMode(m);

  // SoftAP up before any scanning starts; channel is compiled-in so the
  // AP always comes back to the same channel between hop cycles
  WiFi.mode(WIFI_AP_STA);
#ifdef S3_DEFENDER_SOFTAP_PASS
  const bool ok = WiFi.softAP(S3_DEFENDER_SOFTAP_SSID,
                              S3_DEFENDER_SOFTAP_PASS,
                              S3_DEFENDER_SOFTAP_CHANNEL, 0, 4);
#else
  const bool ok = WiFi.softAP(S3_DEFENDER_SOFTAP_SSID, nullptr,
                              S3_DEFENDER_SOFTAP_CHANNEL, 0, 4);
#endif
  Serial.printf("SoftAP %s %s — AP IP %s max stations 4\n",
                S3_DEFENDER_SOFTAP_SSID, ok ? "up" : "FAILED",
                WiFi.softAPIP().toString().c_str());
  return ok;
}

void tick() { // loopTask
  if (s_savePending.load() &&
      (int32_t)(millis() - s_saveAtMs.load()) >= 0) {
    if (s_prefsOk) s_prefs.putUChar("mode", s_mode.load());
    s_savePending.store(false);
  }
  Mode m = mode();
  applyMode((uint8_t)m);
  // keep display's mode string fresh (ui task snapshot reads app::mode too)
  (void)m;
}

Mode mode() { return (Mode)s_mode.load(); }

const char* modeName() { return modeNameFor(s_mode.load()); }

void modeName(char* out, uint8_t max) {
  const char* n = modeName();
  strncpy(out, n, max);
}

void setMode(Mode m) {
  if (s_mode.exchange((uint8_t)m) != (uint8_t)m) {
    s_savePending.store(true);
    s_saveAtMs.store(millis() + 1000); // NVS wear debounce
    Serial.printf("app: mode -> %s\n", modeNameFor((uint8_t)m));
  }
}

bool setModeName(const char* name, uint8_t len) {
  if (!name) return false;
  char buf[8];
  if (len > (int)sizeof(buf) - 1) return false;
  memcpy(buf, name, len);
  buf[len] = 0;
  uint8_t m;
  if (!parseMode(buf, len, &m)) return false;
  setMode((Mode)m);
  return true;
}

void cycleMode() {
  uint8_t m = s_mode.load();
  m = m == (uint8_t)Mode::Wifi  ? (uint8_t)Mode::Ble
      : m == (uint8_t)Mode::Ble ? (uint8_t)Mode::Both
                                : (uint8_t)Mode::Wifi;
  setMode((Mode)m);
}

void toggleDisplayState() {
  switch (display::state()) {
    case display::State::On: display::setState(display::State::Dim); break;
    case display::State::Dim: display::setState(display::State::Sleep); break;
    case display::State::Sleep: display::setState(display::State::On); break;
  }
}

uint32_t epochS() {
  uint32_t base = s_epochBase.load();
  return base ? base + (uint32_t)(millis() / 1000) : 0;
}

void setEpoch(uint32_t s) {
  if (s) s_epochBase.store(s - (uint32_t)(millis() / 1000));
}

uint32_t wifiNextScanInSec() { return wifiscan::nextScanInSec(); }

} // namespace app