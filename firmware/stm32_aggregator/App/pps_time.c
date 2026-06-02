#include "pps_time.h"
#include "proto.h"
#include "pal.h"

#include <stdio.h>
#include <string.h>

#ifdef USE_HAL_DRIVER
/* On-target build (HAL present): real PRIMASK control via CMSIS-Core. */
#include "stm32h7xx.h"
static inline uint32_t pps_irq_save(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}
static inline void pps_irq_restore(uint32_t primask) {
    if (!primask) __enable_irq();
}
#else
/* Host-smoke: no IRQ to mask. The single-threaded host test exercises only
 * the linear field updates, so the no-op critical section is safe. */
static inline uint32_t pps_irq_save(void) { return 0; }
static inline void pps_irq_restore(uint32_t primask) { (void)primask; }
#endif

typedef struct {
    uint64_t pps_timer_us;
    uint32_t epoch_s;
    uint32_t pps_count;
    uint32_t last_apply_ms;
    bool     pps_pending;
    bool     time_valid;
    bool     fix_ok;
} pps_state_t;

static pps_state_t g_state;

void pps_time_init(void) {
    uint32_t pm = pps_irq_save();
    memset(&g_state, 0, sizeof(g_state));
    pps_irq_restore(pm);
}

void pps_time_on_edge(uint64_t timer_us) {
    /* Called from EXTI10 callback (IRQ context). PRIMASK is already set by
     * hardware; the bookkeeping here is single-writer/single-reader within
     * one core and the brief save/restore makes the apply-side race-free. */
    uint32_t pm = pps_irq_save();
    g_state.pps_timer_us = timer_us;
    g_state.pps_count++;
    g_state.pps_pending = true;
    pps_irq_restore(pm);
}

void pps_time_apply_from_gps(uint32_t epoch_s, bool fix_ok) {
    uint32_t pm = pps_irq_save();
    g_state.epoch_s = epoch_s;
    g_state.fix_ok = fix_ok;
    g_state.time_valid = true;
    g_state.last_apply_ms = pal_time_ms();
    pps_irq_restore(pm);
}

bool pps_time_snapshot(pps_snapshot_t *out) {
    if (!out) return false;
    uint32_t pm = pps_irq_save();
    out->pps_timer_us = g_state.pps_timer_us;
    out->epoch_s      = g_state.epoch_s;
    out->pps_count    = g_state.pps_count;
    out->time_valid   = g_state.time_valid;
    out->fix_ok       = g_state.fix_ok;
    out->pps_pending  = g_state.pps_pending;
    pps_irq_restore(pm);
    return true;
}

bool pps_time_emit_tm_if_pending(void) {
    /* Read-and-clear of pps_pending under the same critical section so a
     * race with another edge cannot make the function emit twice. */
    uint32_t pm = pps_irq_save();
    bool pending = g_state.pps_pending;
    uint32_t epoch_s = g_state.epoch_s;
    bool fix_ok = g_state.fix_ok;
    bool valid = g_state.time_valid;
    if (pending) g_state.pps_pending = false;
    pps_irq_restore(pm);

    if (!pending) return false;
    if (!valid) return false;

    char line[40];
    snprintf(line, sizeof(line), "$TM,%lu,%u",
             (unsigned long)epoch_s,
             (unsigned)(fix_ok ? 1 : 0));
    if (!proto_finalize_line(line, sizeof(line))) {
        return false;
    }
    for (uint8_t b = 0; b < N_BRANCH_UARTS; b++) {
        pal_branch_tx_write_str(b, line);
    }
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
    pps_snapshot_t s; pps_time_snapshot(&s); return s.time_valid;
}
bool pps_time_fix_ok(void) {
    pps_snapshot_t s; pps_time_snapshot(&s); return s.fix_ok;
}

uint32_t pps_time_last_epoch(void) {
    pps_snapshot_t s; pps_time_snapshot(&s); return s.epoch_s;
}

uint32_t pps_time_pps_count(void) {
    pps_snapshot_t s; pps_time_snapshot(&s); return s.pps_count;
}

uint32_t pps_time_age_ms(void) {
    pps_snapshot_t s; pps_time_snapshot(&s);
    if (s.pps_timer_us == 0) return 0xFFFFFFFFu;
    uint64_t now = pal_time_us_64();
    if (now < s.pps_timer_us) return 0xFFFFFFFFu;
    return (uint32_t)((now - s.pps_timer_us) / 1000ULL);
}

void pps_time_health_check(void) {
    uint32_t pm = pps_irq_save();
    uint64_t base = g_state.pps_timer_us;
    bool was_valid = g_state.time_valid;
    pps_irq_restore(pm);

    if (base == 0) return;
    uint64_t now = pal_time_us_64();
    if ((now - base) > PPS_TIMEOUT_US) {
        pm = pps_irq_save();
        if (was_valid && g_state.time_valid) {
            uint64_t add_s = (now - base) / 1000000ULL;
            g_state.epoch_s += (uint32_t)add_s;
        }
        g_state.time_valid = false;
        pps_irq_restore(pm);
    }
}
