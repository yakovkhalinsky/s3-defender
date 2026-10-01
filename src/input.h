#pragma once
// input — BOOT button on GPIO0, debounced, short/long press detection.
#include <cstdint>

namespace input {

using Cb = void (*)();

bool begin(Cb onShort, Cb onLong);
bool isPressed();

} // namespace input