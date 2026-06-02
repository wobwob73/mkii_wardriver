#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * leaf_ble — BLE-only Leaf (blebt_branch_v1_0.md §7). NimBLE passive observer
 * on an ESP32-S3, all three primary adv channels, 1M + Coded PHYs. Emits
 * $BL / $BX / $BK / $HB on the BC (or lite-aggregator) UART link. BT Classic
 * is a separate binary (leaf_bt_classic) and is NOT built here.
 */

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

#ifndef BLE_DEDUP_WINDOW_MS
#define BLE_DEDUP_WINDOW_MS 1000
#endif

#ifndef BLE_RING_SIZE
#define BLE_RING_SIZE 32
#endif

/* Intra-Leaf dedup set size for the 1 s window (§7.5). */
#ifndef BLE_SEEN_MAX
#define BLE_SEEN_MAX 64
#endif

/* BC link UART. Pins preliminary, pending PCB layout (open item 8). */
#ifndef LEAF_UART_PORT
#define LEAF_UART_PORT 1
#endif
#ifndef LEAF_UART_TX_PIN
#define LEAF_UART_TX_PIN 17
#endif
#ifndef LEAF_UART_RX_PIN
#define LEAF_UART_RX_PIN 18
#endif
#ifndef LEAF_UART_BAUD
#define LEAF_UART_BAUD 230400
#endif

/* $CF modes (blebt §5.1). Light-duty uses only BLE scan (mode 0). */
enum BleLeafMode {
    BLE_MODE_SCAN = 0,   /* BLE passive scan */
    BLE_MODE_BT   = 1,   /* BT Classic — not implemented in this binary */
};

/* Ring slot, mirroring blebt §7.6 BleAdvEvent. */
typedef struct {
    uint64_t local_timer_us;
    uint8_t  bdaddr[6];
    uint8_t  addr_type;        /* 0 public, 1 random static, 2 RPA, 3 NRPA */
    int8_t   rssi;
    uint8_t  channel;          /* 37/38/39, or 0 for ext / unknown */
    uint8_t  adv_type;         /* 0 IND,1 DIRECT,2 NONCONN,3 SCAN_RSP,4 SCAN_IND,5 EXT */
    uint8_t  flags;            /* LE adv flags byte */
    uint8_t  conn;             /* 1 connectable */
    int32_t  company_id;       /* -1 = none */
    uint16_t svc_uuid16;       /* 0 = none */
    uint8_t  phy;              /* 1 1M, 2 2M, 3 Coded S=2, 4 Coded S=8 */
    uint8_t  name_len;
    uint8_t  name[32];
    uint8_t  manuf_len;
    uint8_t  manuf[31];
    uint8_t  svc_list_len;
    uint8_t  svc_list[32];     /* concatenated 16-bit UUIDs (LE) for $BX */
    uint8_t  truncated;        /* 1 → emit $BX */
} BleAdvEvent;

typedef struct {
    bool    adopted;
    char    id_str[8];         /* e.g. "BLE-1" */
    uint8_t mode;
    uint8_t phy_mask;          /* bit0 1M, bit1 Coded; default 3 */
    uint16_t window_ms;
    uint16_t interval_ms;
} BleLeafConfig;
