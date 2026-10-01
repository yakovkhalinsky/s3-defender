#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <pins.h>

#ifndef S3_DEFENDER_SOFTAP_SSID
#define S3_DEFENDER_SOFTAP_SSID "S3-Defender"
#endif

static WebServer server(80);

// TODO(phase2): wifi_scan / ble_scan → scan_store
// TODO(phase3): ST7735 + APA102
enum class ScanMode { Wifi, Ble, Both };
static ScanMode mode = ScanMode::Both;

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>s3-defender</title>
  <style>
    :root { color-scheme: dark; font-family: system-ui, sans-serif; }
    body { margin: 1.5rem; background: #0b1020; color: #e8ecf4; }
    h1 { font-size: 1.25rem; }
    .tag { display: inline-block; padding: 0.15rem 0.5rem; border-radius: 999px;
           background: #1f2a44; color: #9ec1ff; font-size: 0.75rem; }
    p { max-width: 36rem; line-height: 1.45; color: #b8c0d4; }
    code { background: #151b2e; padding: 0.1rem 0.35rem; border-radius: 4px; }
  </style>
</head>
<body>
  <h1>s3-defender <span class="tag">stub UI</span></h1>
  <p>SoftAP is up. Live Wi‑Fi / BLE lists come next.</p>
  <p>SSID: <code>S3-Defender</code></p>
</body>
</html>
)HTML";

static const char* modeName(ScanMode m) {
  switch (m) {
    case ScanMode::Wifi: return "wifi";
    case ScanMode::Ble: return "ble";
    default: return "both";
  }
}

static void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

static void handleStatus() {
  String body = "{";
  body += "\"name\":\"s3-defender\",";
  body += "\"mode\":\"";
  body += modeName(mode);
  body += "\",";
  body += "\"uptime_s\":";
  body += String(millis() / 1000);
  body += ",\"wifi\":[],\"ble\":[]}";
  server.send(200, "application/json", body);
}

static void startSoftAp() {
  WiFi.mode(WIFI_AP);
  const bool ok = WiFi.softAP(S3_DEFENDER_SOFTAP_SSID);
  Serial.printf("SoftAP %s → %s\n", S3_DEFENDER_SOFTAP_SSID, ok ? "ok" : "FAIL");
  Serial.printf("AP IP %s\n", WiFi.softAPIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== s3-defender ===");
  Serial.println("Passive Wi-Fi + BLE scanner (stubs)");
  Serial.printf("BOOT button GPIO %d\n", PIN_BOOT);

  pinMode(PIN_BOOT, INPUT_PULLUP);

  startSoftAp();
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.begin();
  Serial.println("HTTP http://192.168.4.1/");
}

void loop() {
  server.handleClient();

  // TODO: short-press BOOT cycles ScanMode; long-press reserved
  static bool lastBoot = true;
  const bool boot = digitalRead(PIN_BOOT);
  if (lastBoot && !boot) {
    mode = (mode == ScanMode::Wifi)   ? ScanMode::Ble
         : (mode == ScanMode::Ble)    ? ScanMode::Both
                                      : ScanMode::Wifi;
    Serial.printf("mode → %s\n", modeName(mode));
  }
  lastBoot = boot;

  delay(5);
}
