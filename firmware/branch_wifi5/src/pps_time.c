#include "pps_time.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"

#include <string.h>

#define PPS_TIMEOUT_US 2000000ULL

typedef struct {
    uint64_t pps_timer_us;
    uint32_t pps_count;
    uint32_t epoch_s;
    uint32_t last_apply_ms;
    bool     pps_pending;
    bool     time_valid;
    bool     fix_ok;
} pps_state_t;

/* All accesses serialized via g_lock. The PPS GPIO ISR runs on core0; readers
 * and apply_tm run on core1. A bare critical section on either core cannot
 * exclude the other core, so a hardware spinlock is required to keep
 * pps_timer_us (64-bit) from tearing and to prevent the apply path from
 * interleaving with an ISR fire. The lock duration is a few register reads,
 * so the worst-case core0 ISR latency added is negligible. */
static pps_state_t g_state;
static spin_lock_t *g_lock = NULL;

void pps_time_init(void) {
    if (g_lock == NULL) {
        int n = spin_lock_claim_unused(true);
        g_lock = spin_lock_init((uint)n);
    }
    uint32_t saved = spin_lock_blocking(g_lock);
    memset(&g_state, 0, sizeof(g_state));
    spin_unlock(g_lock, saved);
    gpio_init(PPS_GPIO);
    gpio_set_dir(PPS_GPIO, GPIO_IN);
    gpio_pull_down(PPS_GPIO);
}

void pps_time_isr(unsigned int gpio, uint32_t events) {
    (void)events;
    if (gpio != PPS_GPIO) return;
    uint32_t saved = spin_lock_blocking(g_lock);
    g_state.pps_timer_us = time_us_64();
    g_state.pps_count++;
    g_state.pps_pending = true;
    spin_unlock(g_lock, saved);
}

void pps_time_apply_tm(uint32_t epoch_s, bool fix_ok) {
    uint32_t saved = spin_lock_blocking(g_lock);
    if (g_state.pps_pending) {
        g_state.epoch_s = epoch_s;
        g_state.fix_ok = fix_ok;
        g_state.time_valid = true;
        g_state.pps_pending = false;
        g_state.last_apply_ms = to_ms_since_boot(get_absolute_time());
    }
    spin_unlock(g_lock, saved);
}

bool pps_time_snapshot(pps_snapshot_t *out) {
    if (!out) return false;
    uint32_t saved = spin_lock_blocking(g_lock);
    out->pps_timer_us = g_state.pps_timer_us;
    out->epoch_s      = g_state.epoch_s;
    out->pps_count    = g_state.pps_count;
    out->time_valid   = g_state.time_valid;
    out->fix_ok       = g_state.fix_ok;
    out->pps_pending  = g_state.pps_pending;
    spin_unlock(g_lock, saved);
    return true;
}

bool pps_time_compute(uint64_t event_us, uint32_t *out_sec, uint32_t *out_frac_us) {
    if (!out_sec || !out_frac_us) return false;
    pps_snapshot_t s;
    pps_time_snapshot(&s);
    if (s.pps_timer_us == 0) return false;
    int64_t delta = (int64_t)event_us - (int64_t)s.pps_timer_us;
    if (delta < 0) delta = 0;
    uint64_t off = (uint64_t)delta;
    *out_sec = s.epoch_s + (uint32_t)(off / 1000000ULL);
    *out_frac_us = (uint32_t)(off % 1000000ULL);
    return s.time_valid;
}

bool pps_time_is_valid(void) {
    pps_snapshot_t s; pps_time_snapshot(&s);
    return s.time_valid;
}

bool pps_time_fix_ok(void) {
    pps_snapshot_t s; pps_time_snapshot(&s);
    return s.fix_ok;
}

uint32_t pps_time_age_ms(void) {
    pps_snapshot_t s; pps_time_snapshot(&s);
    if (s.pps_timer_us == 0) return UINT32_MAX;
    uint64_t now = time_us_64();
    if (now < s.pps_timer_us) return UINT32_MAX;
    return (uint32_t)((now - s.pps_timer_us) / 1000ULL);
}

uint32_t pps_time_last_epoch(void) {
    pps_snapshot_t s; pps_time_snapshot(&s);
    return s.epoch_s;
}

void pps_time_health_check(void) {
    uint32_t saved = spin_lock_blocking(g_lock);
    uint64_t base = g_state.pps_timer_us;
    if (base == 0) {
        spin_unlock(g_lock, saved);
        return;
    }
    uint64_t now = time_us_64();
    if ((now - base) > PPS_TIMEOUT_US) {
        if (g_state.time_valid) {
            uint64_t add_s = (now - base) / 1000000ULL;
            g_state.epoch_s += (uint32_t)add_s;
        }
        g_state.time_valid = false;
    }
    spin_unlock(g_lock, saved);
}
