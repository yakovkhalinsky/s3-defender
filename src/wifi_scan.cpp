#include "wifi_scan.h"

#include <Arduino.h>
#include <WiFi.h>
#include <atomic>

#include "config.h"
#include "scan_store.h"

namespace wifiscan {
namespace {

#ifdef S3_DEFENDER_SCAN_PAUSE_WITH_CLIENTS
const bool s_pauseWithClients = true;
#else
const bool s_pauseWithClients = false;
#endif

TaskHandle_t  s_task = nullptr;
State         s_state = State::Off;
std::atomic<bool> s_enabled{true}; // app toggles by mode
std::atomic<bool> s_client{false}; // a WS client is mounted
std::atomic<uint32_t> s_nextCycleMs{0};

void setState(State st) { s_state = st; }

void scanTask(void*) {
  for (;;) {
    if (!s_enabled.load()) {
      setState(State::Off);
      vTaskDelay(pdMS_TO_TICKS(300));
      continue;
    }
    const bool pauseForClients =
        s_client.load() && s_pauseWithClients;
    if (pauseForClients) {
      setState(State::Idle);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    setState(State::Scanning);
    // free the PREVIOUS results first or every cycle leaks a heap array
    WiFi.scanDelete();
    const uint32_t dwell = s_client.load() ? S3_DEFENDER_WIFI_DWELL_BUSY_MS
                                           : S3_DEFENDER_WIFI_DWELL_MS;
    const int32_t started =
        WiFi.scanNetworks(/*async=*/true, /*hidden=*/true, /*passive=*/false, dwell);
    if (started == WIFI_SCAN_FAILED) {
      setState(State::Error);
      Serial.println("wifi scan: start failed");
      vTaskDelay(pdMS_TO_TICKS(3000));
      continue;
    }
    // async scan: results are polled; scanComplete returns count when done
    int32_t n = WIFI_SCAN_RUNNING;
    for (int tries = 0;
         (n = WiFi.scanComplete()) == WIFI_SCAN_RUNNING && tries < 2000; ++tries)
      vTaskDelay(pdMS_TO_TICKS(10));
    if (n < 0) {
      setState(State::Idle);
      s_nextCycleMs.store(millis() + S3_DEFENDER_WIFI_SCAN_PERIOD_MS / 2);
      vTaskDelay(pdMS_TO_TICKS(S3_DEFENDER_WIFI_SCAN_PERIOD_MS / 2));
      continue;
    }
    const uint32_t now = millis();
    for (int32_t i = 0; i < n; ++i) {
      wifiscan::Record r{};
      String ssid = WiFi.SSID(i);
      strncpy(r.ssid, ssid.c_str(), sizeof(r.ssid) - 1);
      r.hidden = r.ssid[0] == 0;
      const uint8_t* b = WiFi.BSSID(i);
      if (b) memcpy(r.bssid, b, 6);
      int32_t rssi = WiFi.RSSI(i);
      r.rssi = (int8_t)(rssi < -128 ? -128 : (rssi > 0 ? 0 : rssi));
      r.channel = (uint8_t)WiFi.channel(i);
      r.auth = (uint8_t)WiFi.encryptionType(i);
      store::upsertWifi(r, now);
    }
    WiFi.scanDelete();
    setState(State::Idle);
    s_nextCycleMs.store(millis() + S3_DEFENDER_WIFI_SCAN_PERIOD_MS);
    // sleep the remainder of the cycle outside the radio
    vTaskDelay(pdMS_TO_TICKS(S3_DEFENDER_WIFI_SCAN_PERIOD_MS));
  }
}

} // namespace

const char* authName(uint8_t auth) {
  switch ((wifi_auth_mode_t)auth) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "wep";
    case WIFI_AUTH_WPA_PSK: return "wpa";
    case WIFI_AUTH_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "wpa/wpa2";
    case WIFI_AUTH_ENTERPRISE: return "ent";
    case WIFI_AUTH_WPA3_PSK: return "wpa3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2/wpa3";
    case WIFI_AUTH_WAPI_PSK: return "wapi";
    case WIFI_AUTH_WPA3_ENT_192: return "wpa3ent";
    default: return "?";
  }
}

bool begin() {
  if (s_task) return true;
  s_enabled.store(true);
  return xTaskCreatePinnedToCore(scanTask, "wifi_scan", 6144, nullptr, 2,
                                 &s_task, 1) == pdPASS;
}

void stop() {
  if (s_task) vTaskDelete(s_task);
  s_task = nullptr;
  setState(State::Off);
}

void setEnabled(bool on) { s_enabled.store(on); }
bool enabled() { return s_enabled.load(); }
State getState() { return s_state; }

void setClientPresent(bool present) { s_client.store(present); }

uint8_t nextScanInSec() {
  uint32_t t = s_nextCycleMs.load();
  if (t == 0) return 0;
  uint32_t now = millis();
  if ((int32_t)(t - now) <= 0) return 0;
  uint32_t d = t - now;
  return (uint8_t)(d > 1000 ? d / 1000 : 0);
}

} // namespace wifiscan