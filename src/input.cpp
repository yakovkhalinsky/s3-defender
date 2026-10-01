#include "input.h"

#include <Arduino.h>
#include <atomic>

#include "config.h"
#include "pins.h"

namespace input {
namespace {

TaskHandle_t s_task = nullptr;
Cb           s_onShort = nullptr;
Cb           s_onLong = nullptr;
std::atomic<bool> s_pressed{false};

void inputTask(void*) {
  bool stable = true; // last debounced level (true = released, active low)
  bool firedLong = false;
  uint32_t downMs = 0;
  for (;;) {
    // two consecutive low samples register a press (40 ms debounce)
    const bool a = digitalRead(PIN_BOOT);
    vTaskDelay(pdMS_TO_TICKS(20));
    const bool b = digitalRead(PIN_BOOT);
    const bool nowPressed = !a && !b;

    if (nowPressed != stable) {
      stable = nowPressed;
      s_pressed.store(nowPressed);
      if (nowPressed) {
        downMs = millis();
        firedLong = false;
      } else if (!firedLong && s_onShort) {
        s_onShort(); // short press: released before the long threshold
      }
    } else if (stable && !firedLong &&
               (uint32_t)(millis() - downMs) >= S3_DEFENDER_LONGPRESS_MS) {
      firedLong = true;
      if (s_onLong) s_onLong(); // long press fires once, still held
    }
  }
}

} // namespace

bool begin(Cb onShort, Cb onLong) {
  if (s_task) return true;
  s_onShort = onShort;
  s_onLong = onLong;
  pinMode(PIN_BOOT, INPUT_PULLUP);
  return xTaskCreatePinnedToCore(inputTask, "input", 2048, nullptr, 1,
                                 &s_task, 1) == pdPASS;
}

bool isPressed() { return s_pressed.load(); }

} // namespace input