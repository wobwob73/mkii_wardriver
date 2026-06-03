#include "leaf_health.h"
#include "leaf_cmd.h"

#include "pico/stdlib.h"
#include "pico/time.h"

#include <string.h>

static leaf_state_t g_leaves[N_LEAVES];

void leaf_health_init(void) {
    memset(g_leaves, 0, sizeof(g_leaves));
    for (uint8_t i = 0; i < N_LEAVES; i++) {
        g_leaves[i].idx = i;
    }
}

leaf_state_t *leaf_health_get(uint8_t idx) {
    if (idx >= N_LEAVES) return NULL;
    return &g_leaves[idx];
}

static bool just_booted(uint32_t uptime_s) {
    return uptime_s < 5;
}

void leaf_health_note_hb(uint8_t idx, uint32_t uptime_s, uint32_t free_heap,
                         uint32_t scan_count, uint32_t err_count) {
    leaf_state_t *l = leaf_health_get(idx);
    if (!l) return;

    bool fresh_boot = just_booted(uptime_s) && (l->uptime_s > uptime_s + 1 || (!l->online));

    l->online = true;
    l->uptime_s = uptime_s;
    l->free_heap = free_heap;
    l->scan_count = scan_count;
    l->err_count = err_count;
    l->last_hb_time_ms = to_ms_since_boot(get_absolute_time());
    l->ping_retries = 0;

    if (fresh_boot || !l->configured) {
        leaf_cmd_send_cf(idx);
        l->configured = true;
        l->cf_retries = 0;
        l->last_cf_sent_ms = l->last_hb_time_ms;
    }
}

void leaf_health_note_bk(uint8_t idx, uint32_t expected, uint32_t received) {
    leaf_state_t *l = leaf_health_get(idx);
    if (!l) return;
    l->ap_expected = expected;
    if (received != expected) l->batch_mismatches++;
    l->ap_received = 0;
}

void leaf_health_note_ap_received(uint8_t idx) {
    leaf_state_t *l = leaf_health_get(idx);
    if (!l) return;
    l->ap_received++;
}

void leaf_health_tick_1s(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    for (uint8_t i = 0; i < N_LEAVES; i++) {
        leaf_state_t *l = &g_leaves[i];
        if (!l->configured && l->cf_retries < LEAF_CF_MAX_RETRIES) {
            if (now - l->last_cf_sent_ms >= LEAF_CF_RETRY_MS) {
                leaf_cmd_send_cf(i);
                l->cf_retries++;
                l->last_cf_sent_ms = now;
            }
            continue;
        }
        if (!l->online) continue;
        uint32_t age = now - l->last_hb_time_ms;
        if (l->ping_retries == 0 && age > LEAF_HB_TIMEOUT_MS) {
            leaf_cmd_send_pg(i);
            l->ping_retries = 1;
        } else if (l->ping_retries == 1 && age > LEAF_PING_RETRY_MS) {
            leaf_cmd_send_pg(i);
            l->ping_retries = 2;
        } else if (l->ping_retries == 2 && age > LEAF_PING_FINAL_MS) {
            l->online = false;
            l->configured = false;
            l->cf_retries = 0;
        }
    }
}

uint8_t leaf_health_status_code(uint8_t idx) {
    leaf_state_t *l = leaf_health_get(idx);
    if (!l) return 0;
    if (!l->online) return 0;
    if (l->ping_retries > 0) return 2;
    return 1;
}
