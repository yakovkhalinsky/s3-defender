#include "storage.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <atomic>

#include "app.h"
#include "config.h"
#include "pins.h"

namespace storage {
namespace {

// SD_MMC and fs objects take mount-RELATIVE paths; "/sdcard" gets prepended
// internally. Using full paths produced "/sdcard/sdcard/..." and failed.
const char* const DIR = "/s3defender";
const char* const CURRENT = "/s3defender/log.csv";
const char* const PREVIOUS = "/s3defender/log.1.csv";
const char* const HEADER = "epoch_s,up_s,kind,id,name,rssi,ch,first,mode\n";

TaskHandle_t s_task = nullptr;
QueueHandle_t s_queue = nullptr;
File s_file;
std::atomic<bool> s_mounted{false};
std::atomic<bool> s_writeFail{false};
std::atomic<bool> s_downloadNow{false};
std::atomic<uint32_t> s_size{0};
std::atomic<uint32_t> s_epoch{0}; // epoch seconds at uptime 0 (0 = unset)
uint32_t s_lastFlush = 0;
uint16_t s_dropRun = 0;

bool mount() {
#if defined(S3_DEFENDER_SD_1BIT)
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
#else
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0, PIN_SD_D1, PIN_SD_D2,
                 PIN_SD_D3);
#endif
  // 20 MHz (SDMMC_FREQ_DEFAULT) prefers stability over speed; see HARDWARE.md
  const bool ok = SD_MMC.begin("/sdcard",
#if defined(S3_DEFENDER_SD_1BIT)
                               true,
#else
                               false,
#endif
                               false, SDMMC_FREQ_DEFAULT, 5);
  if (!ok) return false;
  if (!SD_MMC.exists(DIR)) SD_MMC.mkdir(DIR);
  s_mounted.store(true);
  Serial.printf("sd: mounted (%llu MB card)\n",
                (unsigned long long)(SD_MMC.cardSize() / (1024 * 1024)));
  return true;
}

void emitCsvRow(const Row& r, File& f) {
  const uint32_t epochBase = s_epoch.load();
  const uint32_t up = millis() / 1000;
  const uint32_t epoch = epochBase ? epochBase + up : up;
  const bool needQuote = strpbrk(r.name, ",\"\r\n") != nullptr;
  if (!needQuote) {
    f.printf("%lu,%lu,%c,%s,%s,%d,%u,%u,%s\n", (unsigned long)epoch,
             (unsigned long)up, r.kind, r.id, r.name, (int)r.rssi,
             (unsigned)r.ch, r.first ? 1u : 0u, app::modeName());
    return;
  }
  char quoted[67];
  size_t o = 0;
  quoted[o++] = '"';
  for (const char* p = r.name; *p && o + 2 < sizeof(quoted); ++p) {
    if (*p == '"') quoted[o++] = '"';
    quoted[o++] = *p;
  }
  quoted[o++] = '"';
  quoted[o] = 0;
  f.printf("%lu,%lu,%c,%s,%s,%d,%u,%u,%s\n", (unsigned long)epoch,
           (unsigned long)up, r.kind, r.id, quoted, (int)r.rssi,
           (unsigned)r.ch, r.first ? 1u : 0u, app::modeName());
}

void openLog() {
  if (s_file) return;
  s_file = SD_MMC.open(CURRENT, FILE_APPEND);
  if (s_file) {
    if (s_file.size() == 0) s_file.print(HEADER);
    s_size.store((uint32_t)s_file.size());
  } else {
    s_writeFail.store(true);
    Serial.println("sd: open log.csv failed");
  }
}

void rotate() {
  if (s_file) s_file.close();
  if (SD_MMC.exists(PREVIOUS)) SD_MMC.remove(PREVIOUS);
  SD_MMC.rename(CURRENT, PREVIOUS);
  openLog();
  Serial.println("sd: rotated log.csv -> log.1.csv");
}

void tryMount() {
  if (!mount()) return;
  openLog();
  s_lastFlush = millis();
}

void storageTask(void*) {
  for (;;) {
    if (!s_mounted.load()) {
      tryMount();
      if (!s_mounted.load()) {
        vTaskDelay(pdMS_TO_TICKS(60000)); // re-seat the card and wait
        continue;
      }
    }

    Row r;
    if (xQueueReceive(s_queue, &r, pdMS_TO_TICKS(250)) == pdTRUE) {
      if (!s_file) openLog();
      if (!s_file) {
        s_mounted.store(false); // card vanished; re-mount next minute
        continue;
      }
      emitCsvRow(r, s_file);
      s_size.store((uint32_t)s_file.size());
      s_dropRun = 0;
      if (s_size.load() > S3_DEFENDER_LOG_MAX_BYTES && rotationAllowed())
        rotate();
      continue;
    }
    // idle for this 250 ms window: flush periodically
    if (s_file && (uint32_t)(millis() - s_lastFlush) > 5000) {
      s_file.flush();
      s_lastFlush = millis();
    }
  }
}

} // namespace

bool begin() {
  if (s_queue) return true;
  s_queue = xQueueCreate(64, sizeof(Row));
  return s_queue != nullptr &&
         xTaskCreatePinnedToCore(storageTask, "storage", 4096, nullptr, 1,
                                 &s_task, 1) == pdPASS;
}

bool mounted() { return s_mounted.load(); }
uint64_t cardSize() { return s_mounted.load() ? SD_MMC.cardSize() : 0; }
uint32_t sizeBytes() { return s_size.load(); }
bool lastWriteFailed() { return s_writeFail.load(); }
bool rotationAllowed() { return !s_downloadNow.load(); }
void setDownloading(bool on) { s_downloadNow.store(on); }
void setEpoch(uint32_t s) {
  if (s) s_epoch.store(s - (uint32_t)(millis() / 1000));
}

bool queueRow(const Row& r, uint32_t timeoutMs) {
  if (!s_queue) return false;
  return xQueueSend(s_queue, &r, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

uint8_t logNames(char out[][16], uint8_t max) {
  uint8_t n = 0;
  if (max == 0 || !s_mounted.load()) return 0;
  File f = SD_MMC.open(CURRENT, FILE_READ);
  if (f) {
    strncpy(out[n++], "log.csv", 16);
    f.close();
  }
  if (n < max && SD_MMC.exists(PREVIOUS)) strncpy(out[n++], "log.1.csv", 16);
  return n;
}

const char* pathFor(const char* name) {
  static char path[64];
  snprintf(path, sizeof(path), "%s/%s", DIR, name);
  return path;
}

bool isLogName(const char* name) {
  return strcmp(name, "log.csv") == 0 || strcmp(name, "log.1.csv") == 0;
}

void logsJson(char* out, size_t max) {
  if (!s_mounted.load()) {
    snprintf(out, max, "{\"mounted\":false,\"files\":[]}");
    return;
  }
  const unsigned long cur = (unsigned long)sizeBytes();
  unsigned long prev = 0;
  if (SD_MMC.exists(PREVIOUS)) {
    File f = SD_MMC.open(PREVIOUS, FILE_READ);
    if (f) {
      prev = (unsigned long)f.size();
      f.close();
    }
  }
  if (prev) {
    snprintf(out, max,
             "{\"mounted\":true,\"card_bytes\":%llu,\"used_bytes\":%lu,"
             "\"files\":[{\"file\":\"log.csv\",\"size\":%lu},"
             "{\"file\":\"log.1.csv\",\"size\":%lu}]}",
             (unsigned long long)cardSize(), cur, cur, prev);
  } else {
    snprintf(out, max,
             "{\"mounted\":true,\"card_bytes\":%llu,\"used_bytes\":%lu,"
             "\"files\":[{\"file\":\"log.csv\",\"size\":%lu}]}",
             (unsigned long long)cardSize(), cur, cur);
  }
}

} // namespace storage