#include "web.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <SD_MMC.h>
#include <atomic>
#include <esp_heap_caps.h>

#include "app.h"
#include "ble_scan.h"
#include "config.h"
#include "display.h"
#include "scan_store.h"
#include "storage.h"
#include "web/assets.h"
#include "web/index_html.h"
#include "wifi_scan.h"

namespace web {
namespace {

AsyncWebServer s_server(80);
AsyncWebSocket s_ws("/ws");

JsonDocument     s_doc; // reused arena; keeps its peak allocation
store::WifiEntry s_wEn[S3_DEFENDER_WIFI_MAX];
store::BleEntry  s_bEn[S3_DEFENDER_BLE_MAX];
char             s_json[S3_DEFENDER_JSON_MAX_BYTES + 512];

std::atomic<uint8_t> s_downloads{0}; // in-flight /download streams
uint32_t             s_lastPush = 0;
uint16_t             s_topk = S3_DEFENDER_WS_TOPK;

void bssidStr(const uint8_t a[6], char out[18]) {
  snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", a[0], a[1], a[2], a[3],
           a[4], a[5]);
}

// Minimal JSON string escape for the hand-rolled /api/list path (the
// snapshot path uses ArduinoJson). UTF-8 bytes pass through untouched.
size_t jesc(const char* s, char* out, size_t max) {
  size_t o = 0;
  for (const char* p = s; *p && o + 7 < max; ++p) {
    unsigned char c = (unsigned char)*p;
    switch (c) {
      case '"': out[o++] = '\\'; out[o++] = '"'; break;
      case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
      case '\n': out[o++] = '\\'; out[o++] = 'n'; break;
      case '\r': out[o++] = '\\'; out[o++] = 'r'; break;
      case '\t': out[o++] = '\\'; out[o++] = 't'; break;
      default:
        if (c < 0x20) {
          o += (size_t)snprintf(out + o, max - o, "\\u%04x", c);
        } else {
          out[o++] = (char)c;
        }
        break;
    }
  }
  out[o] = 0;
  return o;
}

const char* wifiStateName(wifiscan::State st) {
  switch (st) {
    case wifiscan::State::Off: return "off";
    case wifiscan::State::Scanning: return "scanning";
    case wifiscan::State::Error: return "error";
    default: return "idle";
  }
}

const char* bleStateName(blescan::State st) {
  switch (st) {
    case blescan::State::Off: return "off";
    case blescan::State::Scanning: return "scanning";
    case blescan::State::Error: return "error";
    default: return "idle";
  }
}

// ---- snapshot --------------------------------------------------------------

// Build the status/snapshot document into the reused arena.
void buildDoc(uint16_t kw, uint16_t kb) {
  s_doc.clear();
  JsonObject root = s_doc.to<JsonObject>();
  root["name"] = "s3-defender";
  root["ver"] = S3_DEFENDER_VERSION;
  root["mode"] = app::modeName();
  root["uptime_s"] = millis() / 1000;
  root["epoch_s"] = app::epochS();

  const uint32_t freeB = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  const uint32_t minFree = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  JsonObject heap = root["heap"].to<JsonObject>();
  heap["free"] = freeB;
  heap["min"] = minFree;
  heap["frag_pct"] = freeB ? (uint8_t)(100 - (largest * 100U) / freeB) : 0;

  JsonObject scan = root["scan"].to<JsonObject>();
  scan["wifi"] = wifiStateName(wifiscan::getState());
  scan["ble"] = bleStateName(blescan::getState());
  scan["next_s"] = app::wifiNextScanInSec();

  root["wifi_count"] = store::wifiCount();
  root["ble_count"] = store::bleCount();

  const uint32_t now = millis();
  JsonArray wa = root["wifi"].to<JsonArray>();
  uint16_t nw =
      store::copyWifi(s_wEn, S3_DEFENDER_WIFI_MAX, store::Sort::RssiDesc);
  if (kw < nw) nw = kw;
  for (uint16_t i = 0; i < nw; ++i) {
    const store::WifiEntry& e = s_wEn[i];
    JsonObject o = wa.add<JsonObject>();
    o["ssid"] = e.r.ssid;
    char bs[18];
    bssidStr(e.r.bssid, bs);
    o["bssid"] = bs;
    o["rssi"] = e.r.rssi;
    o["ch"] = e.r.channel;
    o["auth"] = wifiscan::authName(e.r.auth);
    o["seen_ms"] = (uint32_t)(now - e.m.lastSeenMs);
    o["first_ms"] = (uint32_t)(now - e.m.firstSeenMs);
    o["n"] = e.m.count;
  }

  JsonArray ba = root["ble"].to<JsonArray>();
  uint16_t nb =
      store::copyBle(s_bEn, S3_DEFENDER_BLE_MAX, store::Sort::RssiDesc);
  if (kb < nb) nb = kb;
  for (uint16_t i = 0; i < nb; ++i) {
    const store::BleEntry& e = s_bEn[i];
    JsonObject o = ba.add<JsonObject>();
    o["name"] = e.r.name;
    char bs[18];
    bssidStr(e.r.addr, bs);
    o["addr"] = bs;
    o["at"] = e.r.addrType;
    o["rssi"] = e.r.rssi;
    o["seen_ms"] = (uint32_t)(now - e.m.lastSeenMs);
    o["first_ms"] = (uint32_t)(now - e.m.firstSeenMs);
    o["n"] = e.m.count;
    o["conn"] = e.r.connectable;
    o["adv_len"] = e.r.advLen;
  }

  JsonObject sd = root["sd"].to<JsonObject>();
  sd["mounted"] = storage::mounted();
  sd["card_bytes"] = storage::cardSize();
  sd["used_bytes"] = storage::sizeBytes();
  JsonArray farr = sd["files"].to<JsonArray>();
  char names[2][16];
  uint8_t nfiles = storage::logNames(names, 2);
  for (uint8_t i = 0; i < nfiles; ++i) farr.add<const char*>(names[i]);

  JsonObject wsj = root["ws"].to<JsonObject>();
  wsj["clients"] = s_ws.count();
  wsj["topk"] = s_topk;
}

void push() {
  for (;;) {
    buildDoc(s_topk, s_topk);
    const size_t len = serializeJson(s_doc, s_json, sizeof(s_json));
    if (len >= sizeof(s_json) - 1 && s_topk > 8) {
      s_topk /= 2; // adaptive: fewer rows keeps the frame inside the cap
      continue;
    }
    if (len == 0) return;
    s_ws.textAll(s_json, len);
    return;
  }
}

// ---- chunked /api/list -----------------------------------------------------

struct ListState {
  uint8_t kind = 0; // 0 = wifi, 1 = ble, 2 = all
  bool started = false, wClosed = false, done = false;
  uint16_t iw = 0, ib = 0, nw = 0, nb = 0;
  store::WifiEntry w[S3_DEFENDER_WIFI_MAX];
  store::BleEntry b[S3_DEFENDER_BLE_MAX];
};

const size_t kMaxEntryBytes = 320;

bool emitWifiEntry(const store::WifiEntry& e, char* out, size_t max,
                   bool first) {
  char esc[200], bs[18];
  jesc(e.r.ssid, esc, sizeof(esc));
  bssidStr(e.r.bssid, bs);
  const uint32_t now = millis();
  const int n =
      snprintf(out, max, "%s{\"ssid\":\"%s\",\"bssid\":\"%s\",\"rssi\":%d,"
                         "\"ch\":%u,\"auth\":\"%s\",\"seen_ms\":%u,"
                         "\"first_ms\":%u,\"n\":%u}",
               first ? "" : ",", esc, bs, (int)e.r.rssi,
               (unsigned)e.r.channel, wifiscan::authName(e.r.auth),
               (unsigned)(now - e.m.lastSeenMs),
               (unsigned)(now - e.m.firstSeenMs), e.m.count);
  return n > 0 && (size_t)n < max;
}

bool emitBleEntry(const store::BleEntry& e, char* out, size_t max, bool first) {
  char esc[160], bs[18];
  jesc(e.r.name, esc, sizeof(esc));
  bssidStr(e.r.addr, bs);
  const uint32_t now = millis();
  const int n =
      snprintf(out, max, "%s{\"name\":\"%s\",\"addr\":\"%s\",\"at\":%u,"
                         "\"rssi\":%d,\"conn\":%d,\"adv_len\":%u,"
                         "\"seen_ms\":%u,\"first_ms\":%u,\"n\":%u}",
               first ? "" : ",", esc, bs, e.r.addrType, (int)e.r.rssi,
               e.r.connectable ? 1 : 0, e.r.advLen,
               (unsigned)(now - e.m.lastSeenMs),
               (unsigned)(now - e.m.firstSeenMs), e.m.count);
  return n > 0 && (size_t)n < max;
}

// One emitted piece per chunk call (~250 B max) — no buffer needed at all.
size_t listChunk(ListState& st, uint8_t* out, size_t maxLen) {
  if (st.done) return 0;
  if (maxLen < kMaxEntryBytes + 32) return 0; // would stall; end the stream
  size_t n = 0;

  if (!st.started) {
    const char* head = st.kind == 1 ? "{\"ble\":[" : "{\"wifi\":[";
    const size_t hl = strlen(head);
    memcpy(out, head, hl);
    n = hl;
    st.started = true;
  }

  char piece[kMaxEntryBytes + 16];
  const auto appendPiece = [&](size_t l) {
    memcpy(out + n, piece, l);
    n += l;
  };

  if (st.kind == 0 || st.kind == 2) {
    while (st.iw < st.nw) {
      if (!emitWifiEntry(st.w[st.iw], piece, sizeof(piece), st.iw == 0)) break;
      const size_t l = strlen(piece);
      if (n + l + 1 > maxLen) break; // finish this chunk here, resume next call
      appendPiece(l);
      ++st.iw;
    }
    if (st.iw >= st.nw && st.kind == 0) {
      if (n + 3 <= maxLen) {
        out[n++] = ']';
        out[n++] = '}';
        st.done = true;
      }
      return n;
    }
    if (st.iw >= st.nw && !st.wClosed) {
      if (n + 10 > maxLen) return n;
      memcpy(out + n, "],\"ble\":[", 9);
      n += 9;
      st.wClosed = true;
    }
  }

  if (st.kind == 1 || st.wClosed) {
    while (st.ib < st.nb) {
      if (!emitBleEntry(st.b[st.ib], piece, sizeof(piece), st.ib == 0)) break;
      const size_t l = strlen(piece);
      if (n + l + 1 > maxLen) break;
      appendPiece(l);
      ++st.ib;
    }
    if (st.ib >= st.nb && !st.done) {
      if (n + 3 <= maxLen) {
        out[n++] = ']';
        out[n++] = '}';
        st.done = true;
      }
    }
  }
  return n;
}

// ---- handlers --------------------------------------------------------------

void handleRoot(AsyncWebServerRequest* req) {
  // flash-mapped data: zero copy through AsyncProgmemResponse
  AsyncWebServerResponse* r =
      req->beginResponse(200, "text/html", (const uint8_t*)INDEX_HTML,
                         INDEX_HTML_LEN);
  r->addHeader("Cache-Control", "no-store");
  req->send(r);
}

void handleStatus(AsyncWebServerRequest* req) {
  buildDoc(s_topk, s_topk);
  const size_t len = serializeJson(s_doc, s_json, sizeof(s_json));
  // the response keeps a reference, so hand it a stable String copy
  req->send(200, "application/json", String(s_json));
}

void handleList(AsyncWebServerRequest* req) {
  uint8_t kind = 0;
  if (req->hasParam("kind")) {
    const String k = req->getParam("kind")->value();
    kind = (k == "b") ? 1 : (k == "all") ? 2 : 0;
  }
  uint16_t limit = (uint16_t)(req->hasParam("limit")
                                  ? req->getParam("limit")->value().toInt()
                                  : 64);
  if (kind == 0 && limit > S3_DEFENDER_WIFI_MAX) limit = S3_DEFENDER_WIFI_MAX;
  if (kind == 1 && limit > S3_DEFENDER_BLE_MAX) limit = S3_DEFENDER_BLE_MAX;

  auto st = std::make_shared<ListState>();
  st->kind = kind;
  st->nw = store::copyWifi(st->w, kind == 1 ? 0 : limit,
                           store::Sort::RssiDesc);
  st->nb = store::copyBle(st->b, kind == 0 ? 0 : limit,
                          store::Sort::RssiDesc);
  AsyncWebServerResponse* resp = req->beginChunkedResponse(
      "application/json",
      [st](uint8_t* buf, size_t maxLen, size_t) -> size_t {
        return listChunk(*st, buf, maxLen);
      });
  resp->addHeader("Cache-Control", "no-store");
  req->send(resp);
}

void handleMode(AsyncWebServerRequest* req) {
  if (!req->hasParam("mode", true)) {
    req->send(400, "application/json",
              "{\"error\":\"missing mode parameter\"}");
    return;
  }
  const String v = req->getParam("mode", true)->value();
  if (!app::setModeName(v.c_str(), (uint8_t)v.length())) {
    req->send(400, "application/json",
              "{\"error\":\"mode must be wifi|ble|both\"}");
    return;
  }
  char body[40];
  snprintf(body, sizeof(body), "{\"ok\":true,\"mode\":\"%s\"}",
           app::modeName());
  req->send(200, "application/json", body);
}

void handleDisplay(AsyncWebServerRequest* req) {
  if (!req->hasParam("action", true)) {
    req->send(400, "application/json",
              "{\"error\":\"missing action parameter\"}");
    return;
  }
  const String a = req->getParam("action", true)->value();
  display::State s = display::State::On;
  if (a == "dim")
    s = display::State::Dim;
  else if (a == "sleep")
    s = display::State::Sleep;
  else if (a == "wake")
    s = display::State::On;
  else {
    req->send(400, "application/json",
              "{\"error\":\"action must be wake|dim|sleep\"}");
    return;
  }
  display::setState(s);
  req->send(200, "application/json", "{\"ok\":true}");
}

void handleTime(AsyncWebServerRequest* req) {
  if (!req->hasParam("epoch_s", true)) {
    req->send(400, "application/json",
              "{\"error\":\"missing epoch_s parameter\"}");
    return;
  }
  const long v = req->getParam("epoch_s", true)->value().toInt();
  if (v > 1600000000L) app::setEpoch((uint32_t)v);
  req->send(200, "application/json", "{\"ok\":true}");
}

// read-only directory listing; rows are only ever written by the storage task
void handleLogs(AsyncWebServerRequest* req) {
  char buf[256];
  storage::logsJson(buf, sizeof(buf));
  req->send(200, "application/json", buf);
}

void handleDownload(AsyncWebServerRequest* req) {
  if (!storage::mounted()) {
    req->send(404, "application/json", "{\"error\":\"no sd card\"}");
    return;
  }
  if (!req->hasParam("file")) {
    req->send(400, "application/json",
              "{\"error\":\"missing file parameter\"}");
    return;
  }
  const String file = req->getParam("file")->value();
  if (!storage::isLogName(file.c_str())) {
    req->send(400, "application/json", "{\"error\":\"bad file name\"}");
    return;
  }
  s_downloads.fetch_add(1);
  storage::setDownloading(true);
  req->onDisconnect([]() {
    if (s_downloads.load() > 0) s_downloads.fetch_sub(1);
    if (s_downloads.load() == 0) storage::setDownloading(false);
  });
  // streamed off the card — no whole-file buffering
  req->send(SD_MMC, storage::pathFor(file.c_str()), "text/csv", true);
}

void handleAsset(AsyncWebServerRequest* req, const uint8_t* data, size_t len) {
  req->send(200, "image/png", data, len); // flash-mapped, zero copy
}

} // namespace

void tick() {
  const uint32_t now = millis();
  if ((uint32_t)(now - s_lastPush) < 1000) return;
  s_lastPush = now;
  s_ws.cleanupClients(2);
  const bool present = s_ws.count() > 0;
  wifiscan::setClientPresent(present);
  if (present && s_ws.availableForWriteAll()) push();
}

bool isDownloading() { return s_downloads.load() > 0; }
uint8_t clientCount() { return (uint8_t)s_ws.count(); }

bool begin() {
  s_ws.onEvent([](AsyncWebSocket*, AsyncWebSocketClient* client,
                  AwsEventType type, void*, uint8_t*, size_t) {
    if (type == WS_EVT_CONNECT) {
      client->setCloseClientOnQueueFull(false);
      Serial.printf("ws: connect id=%u ip=%s\n", client->id(),
                    client->remoteIP().toString().c_str());
    } else if (type == WS_EVT_DISCONNECT) {
      Serial.printf("ws: disconnect id=%u\n", client->id());
    } else if (type == WS_EVT_ERROR) {
      Serial.printf("ws: error id=%u\n", client->id());
    }
  });

  s_server.on("/", HTTP_GET, handleRoot);
  s_server.on("/api/status", HTTP_GET, handleStatus);
  s_server.on("/api/list", HTTP_GET, handleList);
  s_server.on("/api/mode", HTTP_POST, handleMode);
  s_server.on("/api/display", HTTP_POST, handleDisplay);
  s_server.on("/api/time", HTTP_POST, handleTime);
  s_server.on("/api/logs", HTTP_GET, handleLogs);
  s_server.on("/download", HTTP_GET, handleDownload);
  s_server.on("/manifest.webmanifest", HTTP_GET,
              [](AsyncWebServerRequest* req) {
                req->send(200, "application/manifest+json",
                          (const uint8_t*)MANIFEST_JSON, MANIFEST_JSON_LEN);
              });
  s_server.on("/icon-192.png", HTTP_GET, [](AsyncWebServerRequest* req) {
    handleAsset(req, ICON_192, ICON_192_LEN);
  });
  s_server.on("/icon-512.png", HTTP_GET, [](AsyncWebServerRequest* req) {
    handleAsset(req, ICON_512, ICON_512_LEN);
  });
  s_server.on("/icon-192-m.png", HTTP_GET, [](AsyncWebServerRequest* req) {
    handleAsset(req, ICON_192_M, ICON_192_M_LEN);
  });
  s_server.on("/icon-512-m.png", HTTP_GET, [](AsyncWebServerRequest* req) {
    handleAsset(req, ICON_512_M, ICON_512_M_LEN);
  });
  s_server.on("/apple-touch-icon.png", HTTP_GET, [](AsyncWebServerRequest* req) {
    handleAsset(req, ICON_180, ICON_180_LEN);
  });
  s_server.on("/favicon-32.png", HTTP_GET, [](AsyncWebServerRequest* req) {
    handleAsset(req, ICON_32, ICON_32_LEN);
  });

  s_server.begin();
  return true;
}

} // namespace web