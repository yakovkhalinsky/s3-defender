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
  uint32_t lastBeat = 0;

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

    // heartbeat: lets any terminal positively detect a running app
    if ((uint32_t)(now - lastBeat) >= 5000) {
      lastBeat = now;
      Serial.printf("beat %lu.%lus heap=%uk w=%u b=%u mode=%s sta=%u ap=%s\n",
                    (unsigned long)(now / 1000), (unsigned long)(now % 1000),
                    (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
                    (unsigned)store::wifiCount(), (unsigned)store::bleCount(),
                    app::modeName(),
                    (unsigned)WiFi.softAPgetStationNum(),
                    WiFi.softAPIP().toString().c_str());
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

static void serialCmd() {
  static char buf[40];
  static uint8_t n = 0;
  char out = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      buf[n] = 0;
      n = 0;
      if (sscanf(buf, "bl %hhu", (unsigned char*)&out) == 1) {
        display::setBacklightRaw((uint8_t)out);
        Serial.printf("bl=%u\n", (unsigned)out);
      } else if (sscanf(buf, "rot %hhu", (unsigned char*)&out) == 1) {
        display::applyRotation((uint8_t)out);
        Serial.printf("rot=%u\n", (unsigned)out);
      } else if (strcmp(buf, "bars") == 0) {
        display::setState(display::State::On);
        display::testBars();
        Serial.println("bars on");
      } else if (strcmp(buf, "barsq") == 0) {
        display::setState(display::State::Sleep);
        Serial.println("bars off");
      } else if (strcmp(buf, "inv") == 0) {
        display::toggleInversion();
        Serial.println("inv toggled");
      } else if (strcmp(buf, "ap") == 0) {
        Serial.printf("wifi mode %d, AP %s, IP %s, stations %u, wl status %d\n",
                      (int)WiFi.getMode(), WiFi.softAPSSID().c_str(),
                      WiFi.softAPIP().toString().c_str(),
                      (unsigned)WiFi.softAPgetStationNum(), (int)WiFi.status());
      } else if (strncmp(buf, "md ", 3) == 0) {
        if (app::setModeName(buf + 3, strlen(buf + 3)))
          Serial.printf("mode -> %s\n", app::modeName());
        else
          Serial.println("md: wifi|ble|both");
      } else if (strcmp(buf, "scanq") == 0) {
        Serial.printf("wifi-state %d, next in ~%us\n",
                      (int)wifiscan::getState(), (unsigned)app::wifiNextScanInSec());
      }
      buf[0] = 0;
      continue;
    }
    if (n < sizeof(buf) - 1) buf[n++] = c;
  }
}

void loop() {
  app::tick(); // mode application + NVS debounce
  serialCmd();
  vTaskDelay(pdMS_TO_TICKS(10));
}