#pragma once

#include <Arduino.h>
#include <stdint.h>

#ifndef LEAF_VERSION
#define LEAF_VERSION "1.0.0"
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
    LEAF_MODE_SCAN = 0,
    LEAF_MODE_WIDS = 1,
};

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

// 5 GHz channel set IDs per wifi5_branch_v1_0.md §5.1.
enum ChannelSetId : uint8_t {
    CSID_NONE       = 0,
    CSID_UNII1      = 1,
    CSID_UNII2A     = 2,
    CSID_UNII1_2A   = 3,
    CSID_UNII2C     = 4,
    CSID_UNII3      = 5,
    CSID_UNII2C_3   = 6,
    CSID_ALL_5G     = 7,
};

struct LeafConfig {
    bool         adopted;
    char         id_str[8];
    LeafMode     mode;
    ChannelSetId channel_set;
    uint16_t     dwell_ms;
    uint32_t     channel_mask;
};

// Canonical 5 GHz channel list in bit-position order
// (matches wifi5_branch_v1_0.md §5.2).
static const uint8_t CHANNELS_5G[] = {
    36, 40, 44, 48,                            // UNII-1
    52, 56, 60, 64,                            // UNII-2A
    100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,  // UNII-2C
    149, 153, 157, 161, 165                    // UNII-3
};
static constexpr uint8_t N_CHANNELS_5G = sizeof(CHANNELS_5G) / sizeof(CHANNELS_5G[0]);

inline bool valid_leaf_id(const char *s) {
    if (!s) return false;
    if (s[0] != 'W' || s[1] != '5' || s[2] != '_') return false;
    if (s[3] < '1' || s[3] > '3') return false;
    if (s[4] != '\0') return false;
    return true;
}

inline uint32_t mask_for_channel_set(ChannelSetId id) {
    switch (id) {
        case CSID_UNII1:    return 0x0000000Fu;
        case CSID_UNII2A:   return 0x000000F0u;
        case CSID_UNII1_2A: return 0x000000FFu;
        case CSID_UNII2C:   return 0x000FFF00u;
        case CSID_UNII3:    return 0x01F00000u;
        case CSID_UNII2C_3: return 0x01FFFF00u;
        case CSID_ALL_5G:   return 0x01FFFFFFu;
        default:            return 0;
    }
}
