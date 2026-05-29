#include "bssid_tracker.h"

#include <string.h>

namespace bssid_tracker {

struct Slot {
    uint8_t mac[6];
    uint8_t used;
};

static Slot     g_slots[WIDS_SEEN_BSSID_MAX];
static uint32_t g_saturation = 0;

static uint32_t fnv1a(const uint8_t *data, size_t n) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < n; i++) {
        h ^= data[i];
        h *= 0x01000193u;
    }
    return h;
}

void init() {
    memset(g_slots, 0, sizeof(g_slots));
    g_saturation = 0;
}

void reset_cycle() {
    memset(g_slots, 0, sizeof(g_slots));
    g_saturation = 0;
}

bool seen_or_insert(const uint8_t bssid[6]) {
    if (!bssid) return true;
    uint32_t h = fnv1a(bssid, 6);
    uint32_t start = h % WIDS_SEEN_BSSID_MAX;
    for (uint32_t i = 0; i < WIDS_SEEN_BSSID_MAX; i++) {
        uint32_t idx = (start + i) % WIDS_SEEN_BSSID_MAX;
        Slot &s = g_slots[idx];
        if (!s.used) {
            memcpy(s.mac, bssid, 6);
            s.used = 1;
            return false;
        }
        if (memcmp(s.mac, bssid, 6) == 0) {
            return true;
        }
    }
    g_saturation++;
    return true;
}

uint32_t saturation_count() { return g_saturation; }

}
