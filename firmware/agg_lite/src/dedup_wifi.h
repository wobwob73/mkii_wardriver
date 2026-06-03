#pragma once

#include "agg_defs.h"

/*
 * WiFi AP dedup — the BC's tombstone dedup (branch_controller_wifi24_v1_0.md
 * §6) made re-entrant so two instances can coexist (W24 + W5G). The single
 * difference from the BC version is that all state lives in a caller-owned
 * wifi_dedup_t rather than file statics.
 */

typedef struct {
    dedup_entry_t tbl[WIFI_DEDUP_CAP];
    uint64_t      window_start_us;
    uint16_t      active;
    uint32_t      tombstone_total;
} wifi_dedup_t;

void wifi_dedup_init(wifi_dedup_t *d);

void wifi_dedup_update(wifi_dedup_t *d, const detection_t *det);

void wifi_dedup_advance_window(wifi_dedup_t *d, uint64_t now_us);

uint16_t wifi_dedup_active_count(const wifi_dedup_t *d);

void wifi_dedup_for_each_pending(wifi_dedup_t *d,
                                 void (*cb)(const dedup_entry_t *, void *),
                                 void *ctx);

void wifi_dedup_mark_emitted(wifi_dedup_t *d, const uint8_t bssid[6]);
