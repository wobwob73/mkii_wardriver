#pragma once

#include "agg_defs.h"

/*
 * BLE dedup — the same tombstone dedup discipline keyed on BD address instead
 * of BSSID (blebt_branch_v1_0.md §9.2: 5 s window, 1024 entries). Strongest
 * RSSI within the window wins; one $BD per unique bdaddr per window.
 */

typedef struct {
    ble_dedup_entry_t tbl[BLE_DEDUP_CAP];
    uint64_t          window_start_us;
    uint16_t          active;
    uint32_t          tombstone_total;
} ble_dedup_t;

void ble_dedup_init(ble_dedup_t *d);

void ble_dedup_update(ble_dedup_t *d, const ble_event_t *ev);

void ble_dedup_advance_window(ble_dedup_t *d, uint64_t now_us);

uint16_t ble_dedup_active_count(const ble_dedup_t *d);

void ble_dedup_for_each_pending(ble_dedup_t *d,
                                void (*cb)(const ble_dedup_entry_t *, void *),
                                void *ctx);

void ble_dedup_mark_emitted(ble_dedup_t *d, const uint8_t bdaddr[6]);
