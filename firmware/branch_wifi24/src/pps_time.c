#include "pps_time.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"

#include <string.h>

#define PPS_TIMEOUT_US 2000000ULL

static pps_time_state_t g_state;

void pps_time_init(void) {
    memset(&g_state, 0, sizeof(g_state));
    gpio_init(PPS_GPIO);
    gpio_set_dir(PPS_GPIO, GPIO_IN);
    gpio_pull_down(PPS_GPIO);
}

void pps_time_isr(uint gpio, uint32_t events) {
    (void)events;
    if (gpio != PPS_GPIO) return;
    g_state.pps_timer_us = time_us_64();
    g_state.pps_count++;
    g_state.pps_pending = true;
}

void pps_time_apply_tm(uint32_t epoch_s, bool fix_ok) {
    if (!g_state.pps_pending) return;
    g_state.epoch_s = epoch_s;
    g_state.fix_ok = fix_ok;
    g_state.time_valid = true;
    g_state.pps_pending = false;
    g_state.last_apply_ms = to_ms_since_boot(get_absolute_time());
}

bool pps_time_compute(uint64_t event_us, uint32_t *out_sec, uint32_t *out_frac_us) {
    if (!out_sec || !out_frac_us) return false;
    uint64_t base = g_state.pps_timer_us;
    if (base == 0) return false;
    int64_t delta = (int64_t)event_us - (int64_t)base;
    if (delta < 0) delta = 0;
    uint64_t off = (uint64_t)delta;
    uint32_t add_s = (uint32_t)(off / 1000000ULL);
    uint32_t frac = (uint32_t)(off % 1000000ULL);
    *out_sec = g_state.epoch_s + add_s;
    *out_frac_us = frac;
    return g_state.time_valid;
}

bool pps_time_is_valid(void) { return g_state.time_valid; }
bool pps_time_fix_ok(void)   { return g_state.fix_ok; }

uint32_t pps_time_age_ms(void) {
    uint64_t now = time_us_64();
    uint64_t base = g_state.pps_timer_us;
    if (base == 0 || now < base) return UINT32_MAX;
    return (uint32_t)((now - base) / 1000ULL);
}

uint32_t pps_time_last_epoch(void) { return g_state.epoch_s; }

void pps_time_health_check(void) {
    uint64_t now = time_us_64();
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
