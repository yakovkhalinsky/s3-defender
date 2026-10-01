#pragma once
// wifi_scan — periodic standard Wi-Fi scan feeding scan_store.
//
// Uses the station scan API (esp_wifi_scan via WiFi.scanNetworks) rather than
// an always-promiscuous channel hop: the SoftAP must stay reachable for the
// web client, and the scan API hops channels internally and restores. This
// sends ordinary probe requests (like any phone does) — see README.
#include <cstdint>

namespace wifiscan {

struct Record {
  char ssid[33];
  uint8_t bssid[6];
  int8_t rssi;
  uint8_t channel;
  uint8_t auth; // wifi_auth_mode_t
  bool hidden;
};

enum class State : uint8_t { Off, Idle, Scanning, Error };

const char* authName(uint8_t auth);

bool  begin();     // spawns the scan task (SoftAP is already up — app owns WiFi)
void  stop();
void  setEnabled(bool on);
bool  enabled();
void  setClientPresent(bool present); // web tick reports WS client state
State getState();
uint8_t nextScanInSec(); // 0 = unknown/running

} // namespace wifiscan