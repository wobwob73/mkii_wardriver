#include "queues.h"

#include "hardware/sync.h"

#include <string.h>

#if (ENV_RING_CAP & (ENV_RING_CAP - 1)) != 0
#error "ENV_RING_CAP must be a power of two"
#endif

static env_sample_t g_ring[ENV_RING_CAP];
static volatile uint32_t g_w = 0;
static volatile uint32_t g_r = 0;
static volatile uint32_t g_overflow = 0;

static inline uint32_t next_idx(uint32_t i) {
    return (i + 1) & (ENV_RING_CAP - 1);
}

void env_q_init(void) {
    g_w = 0;
    g_r = 0;
    g_overflow = 0;
    memset(g_ring, 0, sizeof(g_ring));
}

bool env_q_push(const env_sample_t *s) {
    if (!s) return false;
    uint32_t w = g_w;
    uint32_t r = g_r;
    if (next_idx(w) == r) {
        g_overflow++;
        return false;
    }
    g_ring[w] = *s;
    __dmb();
    g_w = next_idx(w);
    return true;
}

bool env_q_pop(env_sample_t *out) {
    if (!out) return false;
    uint32_t w = g_w;
    uint32_t r = g_r;
    if (w == r) return false;
    __dmb();
    *out = g_ring[r];
    g_r = next_idx(r);
    return true;
}

uint32_t env_q_overflow_count(void) { return g_overflow; }

uint16_t env_q_used(void) {
    uint32_t w = g_w;
    uint32_t r = g_r;
    return (uint16_t)((w - r) & (ENV_RING_CAP - 1));
}
