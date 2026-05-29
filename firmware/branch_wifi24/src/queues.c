#include "queues.h"

#include "hardware/sync.h"

#include <string.h>

#if (DETECTION_QUEUE_CAP & (DETECTION_QUEUE_CAP - 1)) != 0
#error "DETECTION_QUEUE_CAP must be a power of two"
#endif
#if (WIDS_QUEUE_CAP & (WIDS_QUEUE_CAP - 1)) != 0
#error "WIDS_QUEUE_CAP must be a power of two"
#endif

static detection_t g_det_ring[DETECTION_QUEUE_CAP];
static volatile uint32_t g_det_w = 0;
static volatile uint32_t g_det_r = 0;
static volatile uint32_t g_det_overflow = 0;

static wids_event_t g_wids_ring[WIDS_QUEUE_CAP];
static volatile uint32_t g_wids_w = 0;
static volatile uint32_t g_wids_r = 0;
static volatile uint32_t g_wids_overflow = 0;

static inline uint32_t det_next(uint32_t i) {
    return (i + 1) & (DETECTION_QUEUE_CAP - 1);
}
static inline uint32_t wids_next(uint32_t i) {
    return (i + 1) & (WIDS_QUEUE_CAP - 1);
}

void detection_q_init(void) {
    g_det_w = 0;
    g_det_r = 0;
    g_det_overflow = 0;
    memset(g_det_ring, 0, sizeof(g_det_ring));
}

bool detection_q_push(const detection_t *e) {
    if (!e) return false;
    uint32_t w = g_det_w;
    uint32_t r = g_det_r;
    if (det_next(w) == r) {
        g_det_overflow++;
        return false;
    }
    g_det_ring[w] = *e;
    __dmb();
    g_det_w = det_next(w);
    return true;
}

bool detection_q_pop(detection_t *out) {
    if (!out) return false;
    uint32_t w = g_det_w;
    uint32_t r = g_det_r;
    if (w == r) return false;
    __dmb();
    *out = g_det_ring[r];
    g_det_r = det_next(r);
    return true;
}

uint32_t detection_q_overflow_count(void) { return g_det_overflow; }

uint16_t detection_q_used(void) {
    uint32_t w = g_det_w;
    uint32_t r = g_det_r;
    return (uint16_t)((w - r) & (DETECTION_QUEUE_CAP - 1));
}

void wids_q_init(void) {
    g_wids_w = 0;
    g_wids_r = 0;
    g_wids_overflow = 0;
    memset(g_wids_ring, 0, sizeof(g_wids_ring));
}

bool wids_q_push(const wids_event_t *e) {
    if (!e) return false;
    uint32_t w = g_wids_w;
    uint32_t r = g_wids_r;
    if (wids_next(w) == r) {
        g_wids_overflow++;
        return false;
    }
    g_wids_ring[w] = *e;
    __dmb();
    g_wids_w = wids_next(w);
    return true;
}

bool wids_q_pop(wids_event_t *out) {
    if (!out) return false;
    uint32_t w = g_wids_w;
    uint32_t r = g_wids_r;
    if (w == r) return false;
    __dmb();
    *out = g_wids_ring[r];
    g_wids_r = wids_next(r);
    return true;
}

uint32_t wids_q_overflow_count(void) { return g_wids_overflow; }

uint16_t wids_q_used(void) {
    uint32_t w = g_wids_w;
    uint32_t r = g_wids_r;
    return (uint16_t)((w - r) & (WIDS_QUEUE_CAP - 1));
}
