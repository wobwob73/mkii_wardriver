#include "wids.h"
#include "dedup.h"

#include <string.h>

typedef struct {
    uint8_t  src[6];
    uint16_t count;
    uint64_t window_start_us;
    uint64_t last_event_us;
    uint8_t  channel;
    uint8_t  dst[6];
    bool     used;
} deauth_tracker_t;

static deauth_tracker_t g_track[DEAUTH_TRACKER_CAP];

void wids_init(void) {
    memset(g_track, 0, sizeof(g_track));
}

bool wids_check_evil_twin(const wids_event_t *bc, evil_twin_alert_t *out) {
    if (!bc || !out) return false;
    if (bc->type != WIDS_BEACON) return false;

    dedup_entry_t known;
    bool found = dedup_lookup(bc->bssid, &known);

    if (found && known.ssid_len > 0 && bc->ssid_len > 0) {
        if (known.ssid_len != bc->ssid_len ||
            memcmp(known.ssid, bc->ssid, bc->ssid_len) != 0) {
            memset(out, 0, sizeof(*out));
            out->kind = ET_SSID_MISMATCH;
            memcpy(out->rogue_bssid, bc->bssid, 6);
            memcpy(out->known_bssid, bc->bssid, 6);
            memcpy(out->ssid, bc->ssid, bc->ssid_len);
            out->ssid_len = bc->ssid_len;
            out->rogue_enc = bc->enc;
            out->known_enc = known.enc;
            out->rogue_rssi = bc->rssi;
            out->channel = bc->channel;
            out->local_timer_us = bc->local_timer_us;
            return true;
        }
    }

    return false;
}

static deauth_tracker_t *find_or_alloc(const uint8_t src[6], uint64_t now_us) {
    deauth_tracker_t *oldest = &g_track[0];
    deauth_tracker_t *unused = NULL;
    for (uint32_t i = 0; i < DEAUTH_TRACKER_CAP; i++) {
        deauth_tracker_t *t = &g_track[i];
        if (t->used && memcmp(t->src, src, 6) == 0) {
            return t;
        }
        if (!t->used && !unused) {
            unused = t;
        }
        if (t->used && t->window_start_us < oldest->window_start_us) {
            oldest = t;
        }
    }
    deauth_tracker_t *chosen = unused ? unused : oldest;
    memset(chosen, 0, sizeof(*chosen));
    memcpy(chosen->src, src, 6);
    chosen->window_start_us = now_us;
    chosen->used = true;
    return chosen;
}

bool wids_check_deauth(const wids_event_t *de, deauth_alert_t *out) {
    if (!de || !out) return false;
    if (de->type != WIDS_DEAUTH) return false;

    deauth_tracker_t *t = find_or_alloc(de->src, de->local_timer_us);
    if (!t) return false;

    if (de->local_timer_us - t->window_start_us > DEAUTH_FLOOD_WINDOW_US) {
        t->window_start_us = de->local_timer_us;
        t->count = 0;
    }
    t->count++;
    t->last_event_us = de->local_timer_us;
    t->channel = de->channel;
    memcpy(t->dst, de->dst, 6);

    if (t->count >= DEAUTH_FLOOD_THRESHOLD) {
        memset(out, 0, sizeof(*out));
        memcpy(out->src, t->src, 6);
        memcpy(out->dst, t->dst, 6);
        out->count = t->count;
        out->window_start_us = t->window_start_us;
        out->channel = t->channel;
        out->last_event_us = t->last_event_us;
        t->window_start_us = de->local_timer_us;
        t->count = 0;
        return true;
    }
    return false;
}
