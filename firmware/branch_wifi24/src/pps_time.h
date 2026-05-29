#pragma once

#include "branch_defs.h"

typedef struct {
    volatile uint64_t pps_timer_us;
    volatile uint32_t pps_count;
    volatile bool     pps_pending;
    uint32_t          epoch_s;
    bool              time_valid;
    bool              fix_ok;
    uint32_t          last_apply_ms;
} pps_time_state_t;

void pps_time_init(void);

void pps_time_isr(uint gpio, uint32_t events);

void pps_time_apply_tm(uint32_t epoch_s, bool fix_ok);

bool pps_time_compute(uint64_t event_us, uint32_t *out_sec, uint32_t *out_frac_us);

bool pps_time_is_valid(void);

bool pps_time_fix_ok(void);

uint32_t pps_time_age_ms(void);

uint32_t pps_time_last_epoch(void);

void pps_time_health_check(void);
