#pragma once

#include <Arduino.h>
#include <stdint.h>

#ifndef LEAF_VERSION
#define LEAF_VERSION "1.3.0"
#endif

#ifndef MAX_LINE_LEN
#define MAX_LINE_LEN 200
#endif

#ifndef HB_INTERVAL_MS
#define HB_INTERVAL_MS 10000
#endif

#ifndef CONFIG_TIMEOUT_MS
#define CONFIG_TIMEOUT_MS 10000
#endif

#ifndef SCAN_FAIL_RESET_THRESHOLD
#define SCAN_FAIL_RESET_THRESHOLD 10
#endif

#ifndef SCAN_FAIL_REBOOT_THRESHOLD
#define SCAN_FAIL_REBOOT_THRESHOLD 50
#endif

#ifndef HEAP_MIN_BYTES
#define HEAP_MIN_BYTES 16384
#endif

#ifndef WIDS_RING_SIZE
#define WIDS_RING_SIZE 64
#endif

#ifndef WIDS_SEEN_BSSID_MAX
#define WIDS_SEEN_BSSID_MAX 512
#endif

enum LeafMode : uint8_t {
    LEAF_MODE_SCAN    = 0,
    LEAF_MODE_WIDS    = 1,
    LEAF_MODE_SCANHOP = 2,   /* v1.3: passive sweep of a channel set (WH1) */
};

/* Default Scan-Hop hop set: channels 1/6/11 (bits 0/5/10). v1.3 §4.2/§9. */
#ifndef SCANHOP_DEFAULT_MASK
#define SCANHOP_DEFAULT_MASK 1057
#endif
#ifndef SCANHOP_DEFAULT_DWELL_MS
#define SCANHOP_DEFAULT_DWELL_MS 200
#endif

enum LeafEncryption : uint8_t {
    LE_OPEN          = 0,
    LE_WEP           = 1,
    LE_WPA_PSK       = 2,
    LE_WPA2_PSK      = 3,
    LE_WPA_WPA2_PSK  = 4,
    LE_WPA2_ENT      = 5,
    LE_WPA3_PSK      = 6,
    LE_WPA2_WPA3_PSK = 7,
    LE_UNKNOWN       = 8,
    LE_OWE           = 9,
    LE_WPA3_ENT      = 10,
};

enum WidsEventType : uint8_t {
    EVENT_DEAUTH = 0,
    EVENT_PROBE  = 1,
    EVENT_BEACON = 2,
};

struct WidsEvent {
    uint8_t  type;
    uint8_t  subtype;
    uint8_t  src[6];
    uint8_t  dst[6];
    uint8_t  bssid[6];
    uint8_t  ssid[32];
    uint8_t  ssid_len;
    int8_t   rssi;
    uint8_t  channel;
    uint16_t reason;
    uint8_t  enc;
    uint8_t  hidden;
};

struct LeafConfig {
    bool     adopted;
    char     id_str[6];
    LeafMode mode;
    uint8_t  channel;
    uint16_t dwell_ms;
    uint16_t channel_mask;
};

inline bool valid_leaf_id(const char *s) {
    if (!s) return false;
    if (s[0] != 'W') return false;
    /* Scan-Hop convention: WH1 (and WH2.. if ever fielded). v1.3. */
    if (s[1] == 'H') {
        if (s[2] < '1' || s[2] > '9') return false;
        if (s[3] != '\0') return false;
        return true;
    }
    /* Scan / WIDS: W1..W4. */
    if (s[1] < '1' || s[1] > '4') return false;
    if (s[2] != '\0') return false;
    return true;
}
