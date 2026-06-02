#include "dedup_ble.h"

#include <string.h>

static uint32_t fnv1a(const uint8_t *d, size_t n) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < n; i++) {
        h ^= d[i];
        h *= 0x01000193u;
    }
    return h;
}

void ble_dedup_init(ble_dedup_t *d) {
    if (!d) return;
    memset(d->tbl, 0, sizeof(d->tbl));
    d->window_start_us = 0;
    d->active = 0;
    d->tombstone_total = 0;
}

uint16_t ble_dedup_active_count(const ble_dedup_t *d) {
    return d ? d->active : 0;
}

static int find_or_insert_slot(ble_dedup_t *d, const uint8_t bdaddr[6]) {
    uint32_t h = fnv1a(bdaddr, 6);
    uint32_t start = h % BLE_DEDUP_CAP;
    int insert_slot = -1;
    for (uint32_t i = 0; i < BLE_DEDUP_CAP; i++) {
        uint32_t idx = (start + i) % BLE_DEDUP_CAP;
        ble_dedup_entry_t *e = &d->tbl[idx];
        if (e->state == SLOT_EMPTY) {
            return insert_slot >= 0 ? insert_slot : (int)idx;
        }
        if (e->state == SLOT_OCCUPIED && memcmp(e->bdaddr, bdaddr, 6) == 0) {
            return (int)idx;
        }
        if (e->state == SLOT_TOMBSTONE && insert_slot < 0) {
            insert_slot = (int)idx;
        }
    }
    return insert_slot;
}

static int find_slot(ble_dedup_t *d, const uint8_t bdaddr[6]) {
    uint32_t h = fnv1a(bdaddr, 6);
    uint32_t start = h % BLE_DEDUP_CAP;
    for (uint32_t i = 0; i < BLE_DEDUP_CAP; i++) {
        uint32_t idx = (start + i) % BLE_DEDUP_CAP;
        ble_dedup_entry_t *e = &d->tbl[idx];
        if (e->state == SLOT_EMPTY) return -1;
        if (e->state == SLOT_OCCUPIED && memcmp(e->bdaddr, bdaddr, 6) == 0) {
            return (int)idx;
        }
    }
    return -1;
}

void ble_dedup_update(ble_dedup_t *d, const ble_event_t *ev) {
    if (!d || !ev) return;
    int slot = find_or_insert_slot(d, ev->bdaddr);
    if (slot < 0) return;
    ble_dedup_entry_t *e = &d->tbl[slot];
    if (e->state != SLOT_OCCUPIED) {
        memset(e, 0, sizeof(*e));
        memcpy(e->bdaddr, ev->bdaddr, 6);
        e->state = SLOT_OCCUPIED;
        e->addr_type = ev->addr_type;
        e->best_rssi = ev->rssi;
        e->channel = ev->channel;
        e->adv_type = ev->adv_type;
        e->company_id = ev->company_id;
        e->svc_uuid16 = ev->svc_uuid16;
        e->phy = ev->phy;
        memcpy(e->name, ev->name, ev->name_len);
        e->name_len = ev->name_len;
        e->local_timer_us = ev->local_timer_us;
        e->window_start_us = d->window_start_us;
        e->emitted = false;
        d->active++;
        return;
    }

    if (ev->rssi > e->best_rssi) {
        e->best_rssi = ev->rssi;
        e->channel = ev->channel;
        e->local_timer_us = ev->local_timer_us;
    }
    /* Enrich opportunistically: keep the first non-empty name, MSD, UUID. */
    if (ev->name_len > 0 && e->name_len == 0) {
        memcpy(e->name, ev->name, ev->name_len);
        e->name_len = ev->name_len;
    }
    if (e->company_id < 0 && ev->company_id >= 0) e->company_id = ev->company_id;
    if (e->svc_uuid16 == 0 && ev->svc_uuid16 != 0) e->svc_uuid16 = ev->svc_uuid16;
    if (ev->adv_type == 3 /* SCAN_RSP */ && e->adv_type != 3) {
        /* a scan response carries richer data; note its type */
        e->adv_type = ev->adv_type;
    }
}

void ble_dedup_for_each_pending(ble_dedup_t *d,
                                void (*cb)(const ble_dedup_entry_t *, void *),
                                void *ctx) {
    if (!d || !cb) return;
    for (uint32_t i = 0; i < BLE_DEDUP_CAP; i++) {
        ble_dedup_entry_t *e = &d->tbl[i];
        if (e->state == SLOT_OCCUPIED && !e->emitted) {
            cb(e, ctx);
        }
    }
}

void ble_dedup_mark_emitted(ble_dedup_t *d, const uint8_t bdaddr[6]) {
    if (!d) return;
    int slot = find_slot(d, bdaddr);
    if (slot < 0) return;
    d->tbl[slot].emitted = true;
}

/* No periodic compaction (see dedup_wifi.c): tombstones are reused on insert,
 * which keeps the 1024-entry BLE table inside the SRAM budget (§11). */

void ble_dedup_advance_window(ble_dedup_t *d, uint64_t now_us) {
    if (!d) return;
    uint64_t prev_window_start = d->window_start_us;
    d->window_start_us = now_us;

    for (uint32_t i = 0; i < BLE_DEDUP_CAP; i++) {
        ble_dedup_entry_t *e = &d->tbl[i];
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
