#include "pps_time.h"
#include "proto.h"
#include "pal.h"

#include <stdio.h>
#include <string.h>

static pps_state_t g_state;

void pps_time_init(void) {
    memset(&g_state, 0, sizeof(g_state));
}

void pps_time_on_edge(uint64_t timer_us) {
    g_state.pps_timer_us = timer_us;
    g_state.pps_count++;
    g_state.pps_pending = true;
}

void pps_time_apply_from_gps(uint32_t epoch_s, bool fix_ok) {
    g_state.epoch_s = epoch_s;
    g_state.fix_ok = fix_ok;
    g_state.time_valid = true;
    g_state.last_apply_ms = pal_time_ms();
}

bool pps_time_emit_tm_if_pending(void) {
    if (!g_state.pps_pending) return false;
    if (!g_state.time_valid) {
        g_state.pps_pending = false;
        return false;
    }
    char line[40];
    snprintf(line, sizeof(line), "$TM,%lu,%u",
             (unsigned long)g_state.epoch_s,
             (unsigned)(g_state.fix_ok ? 1 : 0));
    if (!proto_finalize_line(line, sizeof(line))) {
        g_state.pps_pending = false;
        return false;
    }
    for (uint8_t b = 0; b < N_BRANCH_UARTS; b++) {
        pal_branch_tx_write_str(b, line);
    }
    g_state.pps_pending = false;
    return true;
}

bool pps_time_compute(uint64_t event_us, uint32_t *out_sec, uint32_t *out_frac_us) {
    if (!out_sec || !out_frac_us) return false;
    uint64_t base = g_state.pps_timer_us;
    if (base == 0) return false;
    int64_t delta = (int64_t)event_us - (int64_t)base;
    if (delta < 0) delta = 0;
    uint64_t off = (uint64_t)delta;
    *out_sec = g_state.epoch_s + (uint32_t)(off / 1000000ULL);
    *out_frac_us = (uint32_t)(off % 1000000ULL);
    return g_state.time_valid;
}

bool pps_time_is_valid(void) { return g_state.time_valid; }
bool pps_time_fix_ok(void)   { return g_state.fix_ok; }
uint32_t pps_time_last_epoch(void) { return g_state.epoch_s; }
uint32_t pps_time_pps_count(void) { return g_state.pps_count; }

uint32_t pps_time_age_ms(void) {
    uint64_t now = pal_time_us_64();
    uint64_t base = g_state.pps_timer_us;
    if (base == 0 || now < base) return 0xFFFFFFFFu;
    return (uint32_t)((now - base) / 1000ULL);
}

void pps_time_health_check(void) {
    uint64_t now = pal_time_us_64();
    uint64_t base = g_state.pps_timer_us;
    if (base == 0) return;
    if ((now - base) > PPS_TIMEOUT_US) {
        if (g_state.time_valid) {
            uint64_t add_s = (now - base) / 1000000ULL;
            g_state.epoch_s += (uint32_t)add_s;
        }
        g_state.time_valid = false;
    }
}
