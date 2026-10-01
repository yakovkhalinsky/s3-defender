#pragma once
// display — ST7735 status screen (TFT_eSPI) + backlight PWM.
//
// The ui task snapshots shared state (Snap below) and calls tick() at 10 Hz.
// Lines are cached; only changed lines redraw. No framebuffer.
#include <cstdint>

namespace display {

enum class State : uint8_t { On, Dim, Sleep };

struct WRow { char name[13]; uint8_t ch; int8_t rssi; };
struct BRow { char name[13]; int8_t rssi; };

struct Snap {
  uint8_t mode;      // app::Mode as uint8_t
  uint16_t wifiCount, bleCount;
  uint8_t wsClients;
  uint16_t heapFreeKb;
  bool sdMounted;
  uint8_t logErr;
  uint8_t nW, nB;    // rows filled (max 3 each)
  WRow w[3];
  BRow b[3];
};

bool begin();
void setState(State s);
State state();
void activityPush();      // ui task on change: wake Dim/Sleep to On
void tick(const Snap& s); // 10 Hz
uint32_t lastActivityMs();

// bring-up helpers (serial console drives these until the panel is verified)
void setBacklightRaw(uint8_t duty);
void testBars();
void applyRotation(uint8_t r);
void toggleInversion();

} // namespace display