#include "dedup_wifi.h"

#include <string.h>

static uint32_t fnv1a(const uint8_t *d, size_t n) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < n; i++) {
        h ^= d[i];
        h *= 0x01000193u;
    }
    return h;
}

void wifi_dedup_init(wifi_dedup_t *d) {
    if (!d) return;
    memset(d->tbl, 0, sizeof(d->tbl));
    d->window_start_us = 0;
    d->active = 0;
    d->tombstone_total = 0;
}

uint16_t wifi_dedup_active_count(const wifi_dedup_t *d) {
    return d ? d->active : 0;
}

static int find_or_insert_slot(wifi_dedup_t *d, const uint8_t bssid[6]) {
    uint32_t h = fnv1a(bssid, 6);
    uint32_t start = h % WIFI_DEDUP_CAP;
    int insert_slot = -1;
    for (uint32_t i = 0; i < WIFI_DEDUP_CAP; i++) {
        uint32_t idx = (start + i) % WIFI_DEDUP_CAP;
        dedup_entry_t *e = &d->tbl[idx];
        if (e->state == SLOT_EMPTY) {
            return insert_slot >= 0 ? insert_slot : (int)idx;
        }
        if (e->state == SLOT_OCCUPIED && memcmp(e->bssid, bssid, 6) == 0) {
            return (int)idx;
        }
        if (e->state == SLOT_TOMBSTONE && insert_slot < 0) {
            insert_slot = (int)idx;
        }
    }
    return insert_slot;
}

static int find_slot(wifi_dedup_t *d, const uint8_t bssid[6]) {
    uint32_t h = fnv1a(bssid, 6);
    uint32_t start = h % WIFI_DEDUP_CAP;
    for (uint32_t i = 0; i < WIFI_DEDUP_CAP; i++) {
        uint32_t idx = (start + i) % WIFI_DEDUP_CAP;
        dedup_entry_t *e = &d->tbl[idx];
        if (e->state == SLOT_EMPTY) return -1;
        if (e->state == SLOT_OCCUPIED && memcmp(e->bssid, bssid, 6) == 0) {
            return (int)idx;
        }
    }
    return -1;
}

void wifi_dedup_update(wifi_dedup_t *d, const detection_t *det) {
    if (!d || !det) return;
    int slot = find_or_insert_slot(d, det->bssid);
    if (slot < 0) return;
    dedup_entry_t *e = &d->tbl[slot];
    if (e->state != SLOT_OCCUPIED) {
        memset(e, 0, sizeof(*e));
        memcpy(e->bssid, det->bssid, 6);
        e->state = SLOT_OCCUPIED;
        e->best_rssi = det->rssi;
        e->best_leaf = det->leaf_idx;
        memcpy(e->ssid, det->ssid, det->ssid_len);
        e->ssid_len = det->ssid_len;
        e->channel = det->channel;
        e->enc = det->enc;
        e->hidden = det->hidden;
        e->local_timer_us = det->local_timer_us;
        e->window_start_us = d->window_start_us;
        e->emitted = false;
        d->active++;
        return;
    }

    if (det->rssi > e->best_rssi) {
        e->best_rssi = det->rssi;
        e->best_leaf = det->leaf_idx;
        e->local_timer_us = det->local_timer_us;
    }
    if (det->ssid_len > 0 && e->ssid_len == 0) {
        memcpy(e->ssid, det->ssid, det->ssid_len);
        e->ssid_len = det->ssid_len;
        e->hidden = 0;
    }
    e->channel = det->channel;
    e->enc = det->enc;
}

void wifi_dedup_for_each_pending(wifi_dedup_t *d,
                                 void (*cb)(const dedup_entry_t *, void *),
                                 void *ctx) {
    if (!d || !cb) return;
    for (uint32_t i = 0; i < WIFI_DEDUP_CAP; i++) {
        dedup_entry_t *e = &d->tbl[i];
        if (e->state == SLOT_OCCUPIED && !e->emitted) {
            cb(e, ctx);
        }
    }
}

void wifi_dedup_mark_emitted(wifi_dedup_t *d, const uint8_t bssid[6]) {
    if (!d) return;
    int slot = find_slot(d, bssid);
    if (slot < 0) return;
    d->tbl[slot].emitted = true;
}

/*
 * No periodic compaction. Tombstones are reclaimed implicitly: find_or_insert
 * reuses the first tombstone in a probe cluster, so tombstone density stays
 * bounded by churn rather than growing without limit. Dropping the scratch-
 * based rehash keeps three coexisting dedup tables inside the RP2040's 264 KB
 * SRAM (§11) — a full-table scratch copy would have doubled each table.
 */

void wifi_dedup_advance_window(wifi_dedup_t *d, uint64_t now_us) {
    if (!d) return;
    uint64_t prev_window_start = d->window_start_us;
    d->window_start_us = now_us;

    for (uint32_t i = 0; i < WIFI_DEDUP_CAP; i++) {
        dedup_entry_t *e = &d->tbl[i];
        if (e->state != SLOT_OCCUPIED) continue;
        if (e->emitted && e->window_start_us == prev_window_start) {
            e->state = SLOT_TOMBSTONE;
            d->tombstone_total++;
            if (d->active > 0) d->active--;
        } else if (!e->emitted) {
            e->window_start_us = d->window_start_us;
            e->emitted = false;
        }
    }
}
