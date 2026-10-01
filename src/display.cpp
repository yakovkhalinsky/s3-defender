#include "display.h"

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "app.h"
#include "config.h"
#include "pins.h"

namespace display {
namespace {

// palette, matching the web UI tokens
const uint16_t C_BG     = ((0x0B & 0xF8) << 8) | ((0x10 & 0xFC) << 3) | (0x20 >> 3);
const uint16_t C_MAIN   = ((0xE8 & 0xF8) << 8) | ((0xEC & 0xFC) << 3) | (0xF4 >> 3);
const uint16_t C_DIM    = ((0x97 & 0xF8) << 8) | ((0xA1 & 0xFC) << 3) | (0xB8 >> 3);
const uint16_t C_ACCENT = ((0x7F & 0xF8) << 8) | ((0xD4 & 0xFC) << 3) | (0xFF >> 3);
const uint16_t C_BAD    = ((0xFF & 0xF8) << 8) | ((0x5F & 0xFC) << 3) | (0x6D >> 3);

TFT_eSPI s_tft;
State    s_state = State::On;
uint32_t s_activity = 0;

char s_line[8][30]; // 0 header, 1-3 wifi, 4-6 ble, 7 footer

// overwrite one text row (10 px) with the given string
void line(const char* text, uint8_t row, uint16_t fg) {
  const uint8_t y = row * 10;
  s_tft.fillRect(0, y, 160, 10, C_BG);
  if (text[0]) s_tft.drawString(text, 1, y + 1, 1);
}

bool drawIfChanged(const char* text, uint8_t row, uint16_t fg) {
  char* cache = s_line[row];
  if (strcmp(cache, text) == 0) return false;
  strncpy(cache, text, sizeof(s_line[0]) - 1);
  cache[sizeof(s_line[0]) - 1] = 0;
  line(text, row, fg);
  return true;
}

} // namespace

bool begin() {
  s_tft.init();
  s_tft.setRotation(S3_DEFENDER_TFT_ROTATION);
  s_tft.fillScreen(C_BG);

  // backlight PWM on GPIO38 (the TFT_eSPI setup does not own TFT_BL)
  ledcSetup(0, 4000, 8);
  ledcAttachPin(PIN_TFT_BL, 0);
  ledcWrite(0, 255);

#if S3_DEFENDER_TFT_TEST
  // one-flash check of color order / inversion / offset:
  // a correct setup shows RED | GREEN | BLUE | WHITE bands in a white frame
  s_tft.fillScreen(TFT_BLACK);
  s_tft.fillRect(0, 20, 40, 40, TFT_RED);
  s_tft.fillRect(40, 20, 40, 40, TFT_GREEN);
  s_tft.fillRect(80, 20, 40, 40, TFT_BLUE);
  s_tft.fillRect(120, 20, 40, 40, TFT_WHITE);
  s_tft.drawRect(0, 0, 160, 80, TFT_WHITE);
  delay(1800);
  s_tft.fillScreen(C_BG);
  s_tft.setTextColor(C_MAIN, C_BG);
#endif
  memset(s_line, 0, sizeof(s_line));
  s_activity = millis();
  return true;
}

void setState(State s) {
  if (s_state == s) return;
  switch (s) {
    case State::On:
      if (s_state == State::Sleep) s_tft.writecommand(0x29); // DISPON
      ledcWrite(0, 255);
      break;
    case State::Dim:
      if (s_state == State::Sleep) s_tft.writecommand(0x29);
      ledcWrite(0, 96);
      break;
    case State::Sleep:
      ledcWrite(0, 0);
      s_tft.writecommand(0x28); // DISPOFF
      break;
  }
  s_state = s;
  s_activity = millis();
}

State state() { return s_state; }

void activityPush() {
  s_activity = millis();
  if (s_state != State::On) setState(State::On); // wake redraws on next tick
}

void tick(const Snap& s) {
  if (s_state == State::Sleep) return;
  char buf[30];
  bool anyDraw = false;

  // header: mode + counts + heap (IP/clients live in the footer)
  snprintf(buf, sizeof(buf), "%-4.4s W%-3uB%-3u %uk",
           app::modeNameFor(s.mode), s.wifiCount, s.bleCount, s.heapFreeKb);
  anyDraw |= drawIfChanged(buf, 0, C_ACCENT);

  // wifi rows
  for (uint8_t i = 0; i < 3; ++i) {
    if (i < s.nW) {
      snprintf(buf, sizeof(buf), "%-11.11s c%-2u %3d", s.w[i].name, s.w[i].ch,
               s.w[i].rssi);
    } else {
      buf[0] = 0;
    }
    anyDraw |= drawIfChanged(buf, 1 + i, C_MAIN);
  }

  // ble rows
  for (uint8_t i = 0; i < 3; ++i) {
    if (i < s.nB) {
      snprintf(buf, sizeof(buf), "%-13.13s %3d", s.b[i].name, s.b[i].rssi);
    } else {
      buf[0] = 0;
    }
    anyDraw |= drawIfChanged(buf, 4 + i, s.b[i].rssi > -80 ? C_MAIN : C_DIM);
  }

  // footer
  snprintf(buf, sizeof(buf), "ws%u sd%c%s", s.wsClients, s.sdMounted ? '+' : '-',
           s.logErr ? " E" : "");
  anyDraw |= drawIfChanged(buf, 7, s.logErr ? C_BAD : C_DIM);

  if (anyDraw) s_activity = millis();
}

uint32_t lastActivityMs() { return s_activity; }

} // namespace display