#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define BRANCH_ID                 "W5G"
#define BC_FW_VERSION             "1.1.0"

#define MAX_LINE_LEN              200
#define UPSTREAM_BAUD             230400
#define LEAF_BAUD                 230400

#define N_LEAVES                  3

#define LEAF_W5_1                 0
#define LEAF_W5_2                 1
#define LEAF_W5_3                 2

#define LEAF_W5_1_RX_PIN          1
#define LEAF_W5_1_TX_PIN          0
#define LEAF_W5_2_RX_PIN          3
#define LEAF_W5_2_TX_PIN          2
#define LEAF_W5_3_RX_PIN          5
#define LEAF_W5_3_TX_PIN          4

#define STM32_UART_INST           uart0
#define STM32_TX_PIN              12
#define STM32_RX_PIN              13

#define PPS_GPIO                  10

#define DETECTION_QUEUE_CAP       256
#define WIDS_QUEUE_CAP            128
#define DEDUP_CAP                 512
#define DEAUTH_TRACKER_CAP        64
#define UPSTREAM_TX_RING          4096

#define DEDUP_WINDOW_US           500000ULL
#define DEAUTH_FLOOD_WINDOW_US    5000000ULL
#define DEAUTH_FLOOD_THRESHOLD    10

#define LEAF_HB_TIMEOUT_MS        15000
#define LEAF_PING_RETRY_MS        18000
#define LEAF_PING_FINAL_MS        21000
#define LEAF_CF_RETRY_MS          2000
#define LEAF_CF_MAX_RETRIES       3
#define LEAF_BOOT_DELAY_MS        3000

#define BRANCH_HB_INTERVAL_MS     10000

typedef enum {
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
} bc_encryption_t;

// Per-Leaf default channel-set ID per wifi5_branch_v1_0.md §6.
typedef enum {
    CSID_UNII1_2A = 3,
    CSID_UNII2C_3 = 6,
    CSID_ALL_5G   = 7,
} bc_channel_set_id_t;

typedef struct {
    uint64_t local_timer_us;
    uint8_t  leaf_idx;
    uint8_t  bssid[6];
    uint8_t  ssid[32];
    uint8_t  ssid_len;
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  enc;
    uint8_t  hidden;
} detection_t;

typedef enum {
    WIDS_DEAUTH = 0,
    WIDS_PROBE  = 1,
    WIDS_BEACON = 2,
} wids_type_t;

typedef struct {
    uint64_t local_timer_us;
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
    uint8_t  leaf_idx;
} wids_event_t;

static inline const char *leaf_id_str(uint8_t idx) {
    switch (idx) {
        case LEAF_W5_1: return "W5_1";
        case LEAF_W5_2: return "W5_2";
        case LEAF_W5_3: return "W5_3";
        default:        return "W5_?";
    }
}

static inline int leaf_idx_from_id(const char *id) {
    if (!id) return -1;
    if (id[0] != 'W' || id[1] != '5' || id[2] != '_' || id[4] != '\0') return -1;
    switch (id[3]) {
        case '1': return LEAF_W5_1;
        case '2': return LEAF_W5_2;
        case '3': return LEAF_W5_3;
        default:  return -1;
    }
}

static inline uint8_t leaf_default_mode(uint8_t idx) {
    return (idx == LEAF_W5_3) ? 1 : 0;
}

static inline bc_channel_set_id_t leaf_default_channel_set(uint8_t idx) {
    switch (idx) {
        case LEAF_W5_1: return CSID_UNII1_2A;
        case LEAF_W5_2: return CSID_UNII2C_3;
        case LEAF_W5_3: return CSID_ALL_5G;
        default:        return CSID_UNII1_2A;
    }
}

static inline uint16_t leaf_default_dwell(uint8_t idx) {
    return (idx == LEAF_W5_3) ? 100 : 200;
}
