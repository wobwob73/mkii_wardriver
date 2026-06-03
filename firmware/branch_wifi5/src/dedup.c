#include "dedup.h"

#include <string.h>

static dedup_entry_t g_tbl[DEDUP_CAP];
static uint64_t      g_window_start_us = 0;
static uint16_t      g_active = 0;
static uint32_t      g_tombstone_total = 0;

static uint32_t fnv1a(const uint8_t *d, size_t n) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < n; i++) {
        h ^= d[i];
        h *= 0x01000193u;
    }
    return h;
}

void dedup_init(void) {
    memset(g_tbl, 0, sizeof(g_tbl));
    g_window_start_us = 0;
    g_active = 0;
    g_tombstone_total = 0;
}

uint16_t dedup_active_count(void) { return g_active; }
uint32_t dedup_tombstone_count(void) { return g_tombstone_total; }

static int find_or_insert_slot(const uint8_t bssid[6]) {
    uint32_t h = fnv1a(bssid, 6);
    uint32_t start = h % DEDUP_CAP;
    int insert_slot = -1;
    for (uint32_t i = 0; i < DEDUP_CAP; i++) {
        uint32_t idx = (start + i) % DEDUP_CAP;
        dedup_entry_t *e = &g_tbl[idx];
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

static int find_slot(const uint8_t bssid[6]) {
    uint32_t h = fnv1a(bssid, 6);
    uint32_t start = h % DEDUP_CAP;
    for (uint32_t i = 0; i < DEDUP_CAP; i++) {
        uint32_t idx = (start + i) % DEDUP_CAP;
        dedup_entry_t *e = &g_tbl[idx];
        if (e->state == SLOT_EMPTY) return -1;
        if (e->state == SLOT_OCCUPIED && memcmp(e->bssid, bssid, 6) == 0) {
            return (int)idx;
        }
    }
    return -1;
}

void dedup_update(const detection_t *d) {
    if (!d) return;
    int slot = find_or_insert_slot(d->bssid);
    if (slot < 0) return;
    dedup_entry_t *e = &g_tbl[slot];
    if (e->state != SLOT_OCCUPIED) {
        memset(e, 0, sizeof(*e));
        memcpy(e->bssid, d->bssid, 6);
        e->state = SLOT_OCCUPIED;
        e->best_rssi = d->rssi;
        e->best_leaf = d->leaf_idx;
        memcpy(e->ssid, d->ssid, d->ssid_len);
        e->ssid_len = d->ssid_len;
        e->channel = d->channel;
        e->enc = d->enc;
        e->hidden = d->hidden;
        e->local_timer_us = d->local_timer_us;
        e->window_start_us = g_window_start_us;
        e->emitted = false;
        g_active++;
        return;
    }

    if (d->rssi > e->best_rssi) {
        e->best_rssi = d->rssi;
        e->best_leaf = d->leaf_idx;
        e->local_timer_us = d->local_timer_us;
    }
    if (d->ssid_len > 0 && e->ssid_len == 0) {
        memcpy(e->ssid, d->ssid, d->ssid_len);
        e->ssid_len = d->ssid_len;
        e->hidden = 0;
    }
    e->channel = d->channel;
    e->enc = d->enc;
}

bool dedup_lookup(const uint8_t bssid[6], dedup_entry_t *out) {
    int slot = find_slot(bssid);
    if (slot < 0) return false;
    if (out) *out = g_tbl[slot];
    return true;
}

void dedup_for_each_pending(void (*cb)(const dedup_entry_t *, void *), void *ctx) {
    if (!cb) return;
    for (uint32_t i = 0; i < DEDUP_CAP; i++) {
        dedup_entry_t *e = &g_tbl[i];
        if (e->state == SLOT_OCCUPIED && !e->emitted) {
            cb(e, ctx);
        }
    }
}

void dedup_mark_emitted(const uint8_t bssid[6]) {
    int slot = find_slot(bssid);
    if (slot < 0) return;
    g_tbl[slot].emitted = true;
}

static void compact_if_needed(void) {
    if (g_tombstone_total < (DEDUP_CAP / 2)) return;

    dedup_entry_t scratch[DEDUP_CAP];
    memset(scratch, 0, sizeof(scratch));
    uint16_t kept = 0;
    for (uint32_t i = 0; i < DEDUP_CAP; i++) {
        if (g_tbl[i].state != SLOT_OCCUPIED) continue;
        dedup_entry_t copy = g_tbl[i];
        uint32_t h = fnv1a(copy.bssid, 6);
        uint32_t start = h % DEDUP_CAP;
        for (uint32_t j = 0; j < DEDUP_CAP; j++) {
            uint32_t idx = (start + j) % DEDUP_CAP;
            if (scratch[idx].state == SLOT_EMPTY) {
                scratch[idx] = copy;
                scratch[idx].state = SLOT_OCCUPIED;
                kept++;
                break;
            }
        }
    }
    memcpy(g_tbl, scratch, sizeof(g_tbl));
    g_active = kept;
    g_tombstone_total = 0;
}

void dedup_advance_window(uint64_t now_us) {
    uint64_t prev_window_start = g_window_start_us;
    g_window_start_us = now_us;

    for (uint32_t i = 0; i < DEDUP_CAP; i++) {
        dedup_entry_t *e = &g_tbl[i];
        if (e->state != SLOT_OCCUPIED) continue;
        if (e->emitted && e->window_start_us == prev_window_start) {
            e->state = SLOT_TOMBSTONE;
            g_tombstone_total++;
            if (g_active > 0) g_active--;
        } else if (!e->emitted) {
            e->window_start_us = g_window_start_us;
            e->emitted = false;
        }
    }
    compact_if_needed();
}
