#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Light-Duty Single-Box Aggregator (agg_lite) — shared definitions.
 *
 * Implements lite_aggregator_v1_0.md: one RP2040 ingesting three Leaves
 * directly (2.4 GHz scan-hop, 5 GHz scan, BLE), disciplining time from its own
 * GPS + 1PPS, and logging deduplicated upstream records to microSD. It plays
 * the Branch-Controller role for three protocol families at once AND the STM32
 * role (own GPS, own $TM, durable SD sink). No STM32, no Jetson.
 *
 * One version string, three places (CODE_STATUS.md convention):
 *   - AGG_LITE_FW_VERSION here
 *   - the CMake target_compile_definitions AGG_LITE_FW_VERSION
 *   - lite_aggregator_v1_0.md §12.2 build-flag example
 */

#ifndef AGG_LITE_FW_VERSION
#define AGG_LITE_FW_VERSION       "1.0.0"
#endif

#define MAX_LINE_LEN              200
#define LEAF_BAUD                 230400
#define GPS_BAUD                  38400

/* --- Leaf slots (one Leaf per protocol family) ------------------------- */
#define N_LEAVES                  3

#define SLOT_W24                  0   /* ESP32-C3, scan-hop ch 1/6/11, leaf_id WH1  */
#define SLOT_W5G                  1   /* ESP32-C5, 5 GHz scan,        leaf_id W5_1  */
#define SLOT_BLE                  2   /* ESP32-S3, BLE passive,       leaf_id BLE-1 */

/* The two WiFi families share the detection/dedup machinery; index them by
 * their slot (SLOT_W24 / SLOT_W5G) into the per-family tables. */
#define N_WIFI_FAMILIES           2

/* --- Pin map (lite_aggregator_v1_0.md §1; preliminary, pending PCB) ----- */
/* Leaf links: PIO0 SM0-SM2 = RX, PIO1 = TX, matching the lowest three BC slots */
#define LEAF_W24_RX_PIN           1   /* PIO0 SM0 RX */
#define LEAF_W24_TX_PIN           0   /* PIO1     TX */
#define LEAF_W5G_RX_PIN           3   /* PIO0 SM1 RX */
#define LEAF_W5G_TX_PIN           2   /* PIO1     TX */
#define LEAF_BLE_RX_PIN           5   /* PIO0 SM2 RX */
#define LEAF_BLE_TX_PIN           4   /* PIO1     TX */

/* GPS over UART1 (not the I2C path the STM32 uses — §1, open item 1) */
#define GPS_UART_INST             uart1
#define GPS_TX_PIN                8    /* UART1 TX -> GPS RX */
#define GPS_RX_PIN                9    /* UART1 RX <- GPS TX */
#define PPS_GPIO                  10   /* 1PPS rising-edge IRQ */

/* microSD over SPI0 */
#define SD_SPI_INST               spi0
#define SD_SCK_PIN                18
#define SD_MOSI_PIN               19
#define SD_MISO_PIN               16
#define SD_CS_PIN                 17
#define SD_CD_PIN                 22   /* card-detect, optional (see AGG_SD_CARD_DETECT) */

/* Card-detect is OFF by default: the bench microSD adapter is a 6-pin SPI
 * breakout (CLK/MOSI/MISO/CS + power) with no CD line. When 0, the FatFs
 * driver skips CD polling and relies on mount success, and GP22 is left free.
 * Set to 1 for sockets that DO expose a card-detect pin (then SD_CD_PIN with
 * the active-low polarity in sd_hw_config.c applies). */
#ifndef AGG_SD_CARD_DETECT
#define AGG_SD_CARD_DETECT        0
#endif

/* Status LED (Pico onboard) */
#ifndef AGG_LED_PIN
#define AGG_LED_PIN               25
#endif

/* --- Queue / table sizing (§11 memory budget) -------------------------- */
#define WIFI_DET_QUEUE_CAP        128   /* per WiFi family (power of two)   */
#define BLE_QUEUE_CAP             128   /* BLE adv events (power of two)    */
#define BLE_EXT_QUEUE_CAP         32    /* $BX pass-through (power of two)  */

#define WIFI_DEDUP_CAP            512   /* per WiFi family                  */
#define BLE_DEDUP_CAP             1024  /* blebt_branch_v1_0.md §9.2/§14    */

#define WIFI_DEDUP_WINDOW_US      500000ULL    /* 500 ms, as the WiFi BCs   */
#define BLE_DEDUP_WINDOW_US       5000000ULL   /* 5 s, blebt §9.2           */

/* SD RAM ring — burst absorber for write stalls (§8.2). */
#define SD_RING_BYTES             32768

/* --- Timing / leaf-health cadence (mirrors the BC) --------------------- */
#define LEAF_HB_TIMEOUT_MS        15000
#define LEAF_PING_RETRY_MS        18000
#define LEAF_PING_FINAL_MS        21000
#define LEAF_CF_RETRY_MS          2000
#define LEAF_CF_MAX_RETRIES       3
#define LEAF_BOOT_DELAY_MS        3000

#define AGG_HB_INTERVAL_MS        10000   /* $LA every 10 s (§7.5)          */
#define WAIT_TIME_TIMEOUT_MS      20000   /* §10: proceed time_flag=1       */
#define SD_MOUNT_RETRY_MS         5000    /* §8.3                           */

/* GPS fix gate: 3D fix with HDOP < 5.0 before a session file is opened. */
#define FIX_GATE_HDOP_X10_MAX     50

/* --- Encryption enum (identical to the WiFi BCs) ----------------------- */
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
} agg_encryption_t;

/* --- WiFi detection (consumed from $AP; reused from the BC) ------------- */
typedef struct {
    uint64_t local_timer_us;
    uint8_t  leaf_idx;          /* the originating slot (SLOT_W24 / SLOT_W5G) */
    uint8_t  bssid[6];
    uint8_t  ssid[32];
    uint8_t  ssid_len;
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  enc;
    uint8_t  hidden;
} detection_t;

/* WiFi dedup slot (reused from the BC dedup, made re-entrant via a table). */
typedef enum {
    SLOT_EMPTY     = 0,
    SLOT_OCCUPIED  = 1,
    SLOT_TOMBSTONE = 2,
} dedup_state_t;

typedef struct {
    uint8_t  bssid[6];
    int8_t   best_rssi;
    uint8_t  best_leaf;
    uint8_t  ssid[32];
    uint8_t  ssid_len;
    uint8_t  channel;
    uint8_t  enc;
    uint8_t  hidden;
    uint64_t local_timer_us;
    uint64_t window_start_us;
    bool     emitted;
    uint8_t  state;
} dedup_entry_t;

/* --- BLE adv event (consumed from $BL) --------------------------------- */
typedef struct {
    uint64_t local_timer_us;
    uint8_t  bdaddr[6];
    uint8_t  addr_type;
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  adv_type;
    uint8_t  flags;
    uint8_t  conn;
    int32_t  company_id;        /* -1 = none */
    uint16_t svc_uuid16;        /* 0 = none  */
    uint8_t  phy;
    uint8_t  name_len;
    uint8_t  name[16];
} ble_event_t;

/* BLE extended payload ($BX) — pass-through, parsed into bounded fields. */
typedef struct {
    uint64_t local_timer_us;
    char     bdaddr[18];
    char     manuf_hex[64];     /* up to 60 hex chars (30 bytes) + NUL */
    char     svc_hex[80];
    char     name_hex[80];
} ble_ext_t;

/* BLE dedup slot (bdaddr-keyed, 5 s window). */
typedef struct {
    uint8_t  bdaddr[6];
    uint8_t  addr_type;
    int8_t   best_rssi;
    uint8_t  channel;
    uint8_t  adv_type;
    int32_t  company_id;
    uint16_t svc_uuid16;
    uint8_t  phy;
    uint8_t  name_len;
    uint8_t  name[16];
    uint64_t local_timer_us;
    uint64_t window_start_us;
    bool     emitted;
    uint8_t  state;
} ble_dedup_entry_t;

/* --- Per-slot identity --------------------------------------------------
 * The aggregator produces the SAME upstream schemas as the multi-box system,
 * so an Analyzer capture is byte-compatible regardless of topology. Each WiFi
 * slot carries its own branch_id/leaf_id (§7, v1.3 amendment cross-ref). */
static inline const char *slot_branch_id(uint8_t slot) {
    switch (slot) {
        case SLOT_W24: return "W24";
        case SLOT_W5G: return "W5G";
        case SLOT_BLE: return "BLE";
        default:       return "??";
    }
}

static inline const char *slot_leaf_id(uint8_t slot) {
    switch (slot) {
        case SLOT_W24: return "WH1";    /* scan-hop, v1.3 amendment §convention */
        case SLOT_W5G: return "W5_1";
        case SLOT_BLE: return "BLE-1";
        default:       return "?";
    }
}
