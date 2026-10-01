#include "led.h"

#include <Arduino.h>

#include "config.h"
#include "pins.h"

namespace led {
namespace {

Pattern s_pattern = Pattern::Idle; // steady state between flashes
uint32_t s_flashUntil = 0;         // activity flash window (0 = idle)
#ifndef S3_DEFENDER_APA102_RGB
const bool s_rgbOrder = false; // APA102 payload is B,G,R (LilyGO)
#else
const bool s_rgbOrder = true;
#endif

// idle clock low; MSB first; data latched on rising edge
inline void pulse(bool bit) {
  digitalWrite(PIN_APA102_DIN, bit ? HIGH : LOW);
  delayMicroseconds(1);
  digitalWrite(PIN_APA102_CLK, HIGH);
  delayMicroseconds(1);
  digitalWrite(PIN_APA102_CLK, LOW);
}

void sendByte(uint8_t b) {
  for (uint8_t i = 0; i < 8; ++i) {
    pulse(b & 0x80);
    b <<= 1;
  }
}

// one LED frame: 0xE0|bri then B,G,R (or R,G,B with the RGB fallback define)
void write(uint8_t bri, uint8_t r, uint8_t g, uint8_t b, bool on = true) {
  digitalWrite(PIN_APA102_CLK, LOW);
  sendByte(0); sendByte(0); sendByte(0); sendByte(0); // start frame
  sendByte(on ? (uint8_t)(0xE0 | (bri & 0x1F)) : 0xE0);
  if (s_rgbOrder) {
    sendByte(r); sendByte(g); sendByte(b);
  } else {
    sendByte(b); sendByte(g); sendByte(r);
  }
  for (int i = 0; i < 8; ++i) sendByte(0xFF); // end frame (N = 1 LED)
}

enum class Color : uint8_t { Cyan, White, Green, Blue, Red, Off };

void paint(uint8_t bri, Color c) {
  int16_t r = 0, g = 0, b = 0;
  switch (c) {
    case Color::Cyan:  g = 8;  b = 12; break;
    case Color::White: r = 10; g = 10; b = 10; break;
    case Color::Green: g = 20; break;
    case Color::Blue:  b = 14; g = 1; break;
    case Color::Red:   r = 14; break;
    case Color::Off:   break;
  }
  write(bri, (uint8_t)r, (uint8_t)g, (uint8_t)b);
}

} // namespace

bool begin() {
  pinMode(PIN_APA102_DIN, OUTPUT);
  pinMode(PIN_APA102_CLK, OUTPUT);
  digitalWrite(PIN_APA102_CLK, LOW);
  digitalWrite(PIN_APA102_DIN, LOW);
  paint(0, Color::Off); // drive a known-off frame instead of random boot bits
  return true;
}

void flash() { // activity blip: called on new sightings
  s_flashUntil = millis() + 300;
}

void tick() { // 10 Hz; only redraws on phase change so it is cheap
  const uint32_t now = millis();
  static uint32_t lastFrame = 0;

  if (s_flashUntil && now < s_flashUntil) {
    paint(31, Color::White);
    return;
  }
  if (s_flashUntil && now >= s_flashUntil) s_flashUntil = 0;

  // redraw the steady frame at most 10 Hz (breathing needs it)
  if ((uint32_t)(now - lastFrame) < 100 && s_pattern != Pattern::Idle) return;
  lastFrame = now;

  switch (s_pattern) {
    case Pattern::Idle: {
      // breathing cyan, period 3 s
      const uint32_t phase = now % 3000;
      uint8_t bri =
          (uint8_t)(3 + (phase < 1500 ? phase : 3000 - phase) * 9 / 1500);
      paint(bri, Color::Cyan);
      break;
    }
    case Pattern::Activity: s_flashUntil = millis() + 300; break;
    case Pattern::Busy: paint(20, Color::Blue); break;
    case Pattern::Error: paint(31, Color::Red); break;
    case Pattern::Off: paint(0, Color::Off); break;
  }
}

void setPattern(Pattern p) { s_pattern = p; }
Pattern pattern() { return s_pattern; }

} // namespace led