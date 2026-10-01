#include "ble_scan.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <atomic>

#include "config.h"
#include "scan_store.h"

namespace blescan {
namespace {

TaskHandle_t s_task = nullptr;
NimBLEScan*  s_scan = nullptr;
State        s_state = State::Off;
std::atomic<bool> s_enabled{true};
std::atomic<uint32_t> s_adsSeen{0}; // diagnostics: ads seen per cycle

class AdvSink : public NimBLEAdvertisedDeviceCallbacks {
  // runs on the NimBLE host task — keep it a pure copy into the store
  void onResult(NimBLEAdvertisedDevice* d) override {
    s_adsSeen.fetch_add(1);
    if (!s_enabled.load()) return;
    Record r{};
    memcpy(r.addr, d->getAddress().getNative(), 6);
    r.addrType = d->getAddressType();
    int rc = d->getRSSI();
    r.rssi = (int8_t)(rc < -128 ? -128 : rc);
    r.advLen = d->getAdvLength();
    r.connectable = d->isConnectable();
    if (d->haveName()) {
      std::string n = d->getName();
      strncpy(r.name, n.c_str(), sizeof(r.name) - 1);
    }
    store::upsertBle(r, millis());
  }
};

void setState(State st) { s_state = st; }

void scanTask(void*) {
  Serial.printf("ble: task start (scan=%p stack=%u)\n", (void*)s_scan,
                (unsigned)uxTaskGetStackHighWaterMark(nullptr) * 4);
  static AdvSink s_sink;
  s_scan = NimBLEDevice::getScan();
  s_scan->setAdvertisedDeviceCallbacks(&s_sink, /*wantDuplicates=*/false);
  s_scan->setActiveScan(true);
  s_scan->setInterval(100); // ms (window/interval define the duty cycle)
  s_scan->setWindow(50);
  s_scan->setDuplicateFilter(false); // pass every report; the store dedups
  s_scan->setMaxResults(0);          // callbacks only — no per-device heap vector

  for (;;) {
    if (!s_enabled.load()) {
      if (s_scan->isScanning()) s_scan->stop();
      setState(State::Idle);
      vTaskDelay(pdMS_TO_TICKS(300));
      continue;
    }
    setState(State::Scanning);
    // async start; the controller never delivers a completion event under
    // SoftAP coexistence, and a perpetual scan blocks STA scans entirely —
    // so the scan is budget-driven instead: fixed window, then stop, gap,
    // repeat. Wi-Fi scanning takes its slice in between.
    s_scan->start(S3_DEFENDER_BLE_SCAN_S, nullptr, false);
    for (uint32_t waited = 0; s_scan->isScanning() && waited < S3_DEFENDER_BLE_SCAN_S * 1000;
         waited += 100)
      vTaskDelay(pdMS_TO_TICKS(100));
    s_scan->stop();
    Serial.printf("ble: window done, %lu ads seen, %u in store\n",
                  (unsigned long)s_adsSeen.exchange(0),
                  (unsigned)store::bleCount());
    setState(State::Idle);
    vTaskDelay(pdMS_TO_TICKS(S3_DEFENDER_BLE_GAP_MS));
  }
}

} // namespace

bool begin() {
  if (s_task) return true;
  NimBLEDevice::init("s3def");
  s_enabled.store(true);
  bool ok = xTaskCreatePinnedToCore(scanTask, "ble_scan", 4096, nullptr, 2,
                                    &s_task, 1) == pdPASS;
  if (!ok) s_task = nullptr;
  return ok;
}

void stop() {
  if (s_task) vTaskDelete(s_task);
  s_task = nullptr;
  s_scan = nullptr;
  setState(State::Off);
}

void setEnabled(bool on) { s_enabled.store(on); }
bool enabled() { return s_enabled.load(); }
State getState() { return s_state; }

} // namespace blescan