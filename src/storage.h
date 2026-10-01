#pragma once
// storage — microSD CSV append (SD_MMC) + log download plumbing.
//
// Only the storage task ever writes to the card. Producers (wifi/ble) enqueue
// fixed-size rows; the task appends + flushes on drain or every 5 s, rotates
// at S3_DEFENDER_LOG_MAX_BYTES (suppressed while a /download is streaming).
#include <cstdint>
#include <cstddef>

namespace storage {

struct Row {
  char kind;      // 'w' | 'b'
  char id[18];    // "aa:bb:cc:dd:ee:ff"
  char name[33];  // SSID or BLE name; may be ""
  int8_t rssi;
  uint8_t ch;     // wifi channel; 0 for BLE
  bool first;     // first sighting this session
};

bool  begin();               // mount attempt; retries every 60 s if no card
bool  mounted();
uint64_t cardSize();
uint32_t sizeBytes();        // current log.csv size
bool  lastWriteFailed();     // ui task reads this for the LED
bool  rotationAllowed();
void  setDownloading(bool on); // kept for symmetry; web tracks its own too

void  setEpoch(uint32_t s);  // real timestamps when a browser has shared one

bool  queueRow(const Row& r, uint32_t timeoutMs = 0); // producers, never blocks

// read-only helpers (safe from any task): names are "log.csv"/"log.1.csv"
uint8_t logNames(char out[][16], uint8_t max);
const char* pathFor(const char* name);
bool  isLogName(const char* name);
void  logsJson(char* out, size_t max); // {"mounted":..,"files":[{"file":..,"size":..}]}

} // namespace storage