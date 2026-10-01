#pragma once
// app — mode ownership, NVS persistence, cross-module glue (web + input).
#include <cstdint>

namespace app {

enum class Mode : uint8_t { Wifi = 0, Ble = 1, Both = 2 };

const char* modeNameFor(uint8_t m); // "wifi" | "ble" | "both"

bool  begin();
void  tick(); // loopTask only: applies pending mode changes, debounced NVS save

Mode  mode();
const char* modeName();
void  cycleMode();      // BOOT short press
void  setMode(Mode m);  // web /api/mode
bool  setModeName(const char* name, uint8_t len);
void  modeName(char* out, uint8_t max);
void  toggleDisplayState(); // BOOT long press: on -> dim -> sleep -> on

uint32_t epochS();     // 0 until /api/time
void  setEpoch(uint32_t s);

uint32_t wifiNextScanInSec();

} // namespace app