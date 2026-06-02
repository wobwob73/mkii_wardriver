#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define BRANCH_ID                 "W24"
#define BC_FW_VERSION             "1.2.0"

#define MAX_LINE_LEN              200
#define UPSTREAM_BAUD             230400
#define LEAF_BAUD                 230400

#define N_LEAVES                  4

#define LEAF_W1                   0
#define LEAF_W2                   1
#define LEAF_W3                   2
#define LEAF_W4                   3

#define LEAF_W1_RX_PIN            1
#define LEAF_W1_TX_PIN            0
#define LEAF_W2_RX_PIN            3
#define LEAF_W2_TX_PIN            2
#define LEAF_W3_RX_PIN            5
#define LEAF_W3_TX_PIN            4
#define LEAF_W4_RX_PIN            7
#define LEAF_W4_TX_PIN            6

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
        case LEAF_W1: return "W1";
        case LEAF_W2: return "W2";
        case LEAF_W3: return "W3";
        case LEAF_W4: return "W4";
        default:      return "W?";
    }
}

static inline int leaf_idx_from_id(const char *id) {
    if (!id) return -1;
    if (id[0] != 'W' || id[2] != '\0') return -1;
    switch (id[1]) {
        case '1': return LEAF_W1;
        case '2': return LEAF_W2;
        case '3': return LEAF_W3;
        case '4': return LEAF_W4;
        default:  return -1;
    }
}

static inline uint8_t leaf_default_mode(uint8_t idx) {
    return (idx == LEAF_W4) ? 1 : 0;
}

static inline uint8_t leaf_default_channel(uint8_t idx) {
    switch (idx) {
        case LEAF_W1: return 1;
        case LEAF_W2: return 6;
        case LEAF_W3: return 11;
        case LEAF_W4: return 0;
        default:      return 1;
    }
}

static inline uint8_t leaf_default_dwell(uint8_t idx) {
    return (idx == LEAF_W4) ? 100 : 0;
}
