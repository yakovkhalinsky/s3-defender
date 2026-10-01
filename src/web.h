#pragma once
// web — SoftAP HTTP + WebSocket (one AsyncWebServer on port 80).
//
// tick() is called from the ui task and internally paces itself to 1 Hz:
// cleanup dead WS clients, then (if a client is mounted and writable) push a
// complete top-K snapshot. The ui task's stack is enough because all scratch
// buffers are file-static.
#include <cstdint>

namespace web {

bool begin();
void tick();
bool isDownloading();
uint8_t clientCount();

} // namespace web