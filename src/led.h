#pragma once
// led — the single APA102 status LED, bit-banged (no FastLED dependency).
#include <cstdint>

namespace led {

enum class Pattern : uint8_t { Off, Idle, Activity, Busy, Error };

bool begin();
void tick(); // 10 Hz from the ui task
void setPattern(Pattern p);
Pattern pattern();
void flash(); // activity blip: new device entered the store

} // namespace led