#pragma once
// s3-defender tunables. Anything under #ifndef can be overridden from
// platformio.ini build_flags.

#ifndef S3_DEFENDER_VERSION
#define S3_DEFENDER_VERSION "0.1.0"
#endif

#ifndef S3_DEFENDER_SOFTAP_CHANNEL
#define S3_DEFENDER_SOFTAP_CHANNEL 1
#endif

#ifndef S3_DEFENDER_WIFI_MAX
#define S3_DEFENDER_WIFI_MAX 48
#endif

#ifndef S3_DEFENDER_BLE_MAX
#define S3_DEFENDER_BLE_MAX 64
#endif

// store age-out (ms)
#ifndef S3_DEFENDER_WIFI_TTL_MS
#define S3_DEFENDER_WIFI_TTL_MS 120000
#endif

#ifndef S3_DEFENDER_BLE_TTL_MS
#define S3_DEFENDER_BLE_TTL_MS 60000
#endif

// scan cadence
#ifndef S3_DEFENDER_WIFI_SCAN_PERIOD_MS
#define S3_DEFENDER_WIFI_SCAN_PERIOD_MS 6000
#endif

#ifndef S3_DEFENDER_WIFI_DWELL_MS
#define S3_DEFENDER_WIFI_DWELL_MS 100
#endif

#ifndef S3_DEFENDER_WIFI_DWELL_BUSY_MS
#define S3_DEFENDER_WIFI_DWELL_BUSY_MS 60
#endif

#ifndef S3_DEFENDER_BLE_SCAN_S
#define S3_DEFENDER_BLE_SCAN_S 2
#endif

#ifndef S3_DEFENDER_BLE_GAP_MS
#define S3_DEFENDER_BLE_GAP_MS 150
#endif

// snapshot budget: if the serialized frame exceeds this, retry with fewer rows
#ifndef S3_DEFENDER_JSON_MAX_BYTES
#define S3_DEFENDER_JSON_MAX_BYTES 8000
#endif

// display
#ifndef S3_DEFENDER_TFT_ROTATION
#define S3_DEFENDER_TFT_ROTATION 1
#endif

#ifndef S3_DEFENDER_TFT_TEST
#define S3_DEFENDER_TFT_TEST 1 // boot bars: one-flash check of color/offset; set 0 after bring-up
#endif

#ifndef S3_DEFENDER_LONGPRESS_MS
#define S3_DEFENDER_LONGPRESS_MS 1200
#endif

// sd logging
#ifndef S3_DEFENDER_LOG_MAX_BYTES
#define S3_DEFENDER_LOG_MAX_BYTES 8388608 // rotate at 8 MB
#endif

#ifndef S3_DEFENDER_LOG_REFIRE_MS
#define S3_DEFENDER_LOG_REFIRE_MS 30000
#endif

#ifndef S3_DEFENDER_LOG_RSSI_DELTA
#define S3_DEFENDER_LOG_RSSI_DELTA 5
#endif

// heap floor (KB) under which the LED shows error
#ifndef S3_DEFENDER_HEAP_FLOOR_KB
#define S3_DEFENDER_HEAP_FLOOR_KB 32
#endif