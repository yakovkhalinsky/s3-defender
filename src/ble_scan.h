#pragma once
// ble_scan — NimBLE scanner-only stack feeding scan_store.
//
// Callback-only scanning (setMaxResults(0)): nothing is stored by the stack;
// each advertisement becomes a small struct copy pushed into the store.
// Dedup key is address+type; a phone rotating its resolvable random address
// (RPA) will re-appear as "new" roughly every 15 minutes.
#include <cstdint>

namespace blescan {

struct Record {
  char name[25];
  uint8_t addr[6];
  uint8_t addrType; // 0 public, 1 random static, 2 RPA, 3 NRPA
  int8_t rssi;
  uint8_t advLen;
  bool connectable;
};

enum class State : uint8_t { Off, Idle, Scanning, Error };

bool  begin(); // NimBLEDevice::init + task
void  stop();
void  setEnabled(bool on);
bool  enabled();
State getState();

} // namespace blescan