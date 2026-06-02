#pragma once

#include "branch_defs.h"

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

void dedup_init(void);

void dedup_update(const detection_t *d);

bool dedup_lookup(const uint8_t bssid[6], dedup_entry_t *out);

void dedup_advance_window(uint64_t now_us);

uint16_t dedup_active_count(void);

void dedup_for_each_pending(void (*cb)(const dedup_entry_t *, void *), void *ctx);

void dedup_mark_emitted(const uint8_t bssid[6]);

uint32_t dedup_tombstone_count(void);
