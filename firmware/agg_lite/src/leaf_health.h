#pragma once

#include "agg_defs.h"

/* Per-Leaf health watchdog + recovery, reused from the BC (scaled to 3 slots).
 * branch_controller_wifi24_v1_0.md §5. */

typedef struct {
    uint8_t  idx;
    bool     online;
    bool     configured;
    uint32_t last_hb_time_ms;
    uint32_t uptime_s;
    uint32_t free_heap;
    uint32_t scan_count;
    uint32_t err_count;
    uint32_t ap_expected;
    uint32_t ap_received;
    uint32_t batch_mismatches;
    uint8_t  ping_retries;
    uint8_t  cf_retries;
    uint32_t last_cf_sent_ms;
} leaf_state_t;

void leaf_health_init(void);

leaf_state_t *leaf_health_get(uint8_t idx);

void leaf_health_note_hb(uint8_t idx, uint32_t uptime_s, uint32_t free_heap,
                         uint32_t scan_count, uint32_t err_count);

void leaf_health_note_bk(uint8_t idx, uint32_t expected, uint32_t received);

void leaf_health_note_ap_received(uint8_t idx);

void leaf_health_tick_1s(void);

/* 0 = offline, 1 = online, 2 = degraded (ping in progress). */
uint8_t leaf_health_status_code(uint8_t idx);
