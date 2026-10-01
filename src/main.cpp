#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "app.h"
#include "ble_scan.h"
#include "config.h"
#include "display.h"
#include "input.h"
#include "led.h"
#include "scan_store.h"
#include "storage.h"
#include "web.h"
#include "wifi_scan.h"

static void uiTask(void*) {
  uint32_t lastPrune = 0;
  uint32_t lastNew = 0;

  for (;;) {
    const uint32_t now = millis();

    // 1 Hz housekeeping: store aging + web push
    if ((uint32_t)(now - lastPrune) >= 1000) {
      store::prune(now);
      lastPrune = now;
      web::tick();
    }

    // activity = a genuinely new device (not every RSSI refresh)
    const uint32_t newEvents = store::newEvents();
    if (newEvents != lastNew) {
      lastNew = newEvents;
      led::flash();
      display::activityPush();
    }

    // ui snapshot: top-3 rows per kind
    display::Snap snap{};
    snap.mode = (uint8_t)app::mode();
    snap.wifiCount = store::wifiCount();
    snap.bleCount = store::bleCount();
    snap.wsClients = web::clientCount();
    snap.heapFreeKb = (uint16_t)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024);
    snap.sdMounted = storage::mounted();
    snap.logErr = storage::lastWriteFailed();

    store::WifiEntry wTop[3];
    store::BleEntry bTop[3];
    snap.nW = store::copyWifi(wTop, 3, store::Sort::RssiDesc);
    snap.nB = store::copyBle(bTop, 3, store::Sort::RssiDesc);
    for (uint8_t i = 0; i < snap.nW; ++i) {
      strncpy(snap.w[i].name, wTop[i].r.ssid, sizeof(snap.w[i].name) - 1);
      snap.w[i].name[sizeof(snap.w[i].name) - 1] = 0;
      snap.w[i].ch = wTop[i].r.channel;
      snap.w[i].rssi = wTop[i].r.rssi;
    }
    for (uint8_t i = 0; i < snap.nB; ++i) {
      if (bTop[i].r.name[0]) {
        strncpy(snap.b[i].name, bTop[i].r.name, sizeof(snap.b[i].name) - 1);
      } else {
        // unnamed: show the last address bytes
        snprintf(snap.b[i].name, sizeof(snap.b[i].name), ":%02x%02x",
                 bTop[i].r.addr[4], bTop[i].r.addr[5]);
      }
      snap.b[i].name[sizeof(snap.b[i].name) - 1] = 0;
      snap.b[i].rssi = bTop[i].r.rssi;
    }

    // heap guardrail -> red LED (recovering clears back to idle)
    const uint32_t freeKb = snap.heapFreeKb;
    if (freeKb < S3_DEFENDER_HEAP_FLOOR_KB) {
      if (led::pattern() != led::Pattern::Error) led::setPattern(led::Pattern::Error);
    } else if (led::pattern() == led::Pattern::Error) {
      led::setPattern(led::Pattern::Idle);
    }

    // dim after 60 s with no new devices
    if (display::state() == display::State::On &&
        (uint32_t)(now - display::lastActivityMs()) > 60000) {
      display::setState(display::State::Dim);
    }

    display::tick(snap);
    led::tick();

    vTaskDelay(pdMS_TO_TICKS(100)); // 10 Hz
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== s3-defender ===");
  Serial.printf("v%s — passive Wi-Fi + BLE monitor\n", S3_DEFENDER_VERSION);
  Serial.printf("free heap at boot: %u\n", ESP.getFreeHeap());

  store::begin();
  led::begin();
  display::begin(); // TFT init (+ boot bars on first bring-up flash)
  storage::begin(); // mounts in its own task; never blocks boot
  app::begin();     // mode from NVS + SoftAP

  Serial.printf("heap after BLE/display/AP: %u\n", ESP.getFreeHeap());

  wifiscan::begin();
  blescan::begin();
  web::begin();
  input::begin([]() { app::cycleMode(); },
               []() { app::toggleDisplayState(); });

  xTaskCreatePinnedToCore(uiTask, "ui", 4096, nullptr, 1, nullptr, 1);

  Serial.println("web UI: http://192.168.4.1");
  Serial.printf("free heap after init: %u\n", ESP.getFreeHeap());
}

void loop() {
  app::tick(); // mode application + NVS debounce
  vTaskDelay(pdMS_TO_TICKS(10));
}