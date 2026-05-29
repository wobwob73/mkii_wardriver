#pragma once

#include "stm32_config.h"

typedef struct {
    uint64_t pps_timer_us;
    uint32_t epoch_s;
    bool     time_valid;
    bool     fix_ok;
    uint32_t pps_count;
    bool     pps_pending;
    uint32_t last_apply_ms;
} pps_state_t;

void pps_time_init(void);

void pps_time_on_edge(uint64_t timer_us);

void pps_time_apply_from_gps(uint32_t epoch_s, bool fix_ok);

bool pps_time_emit_tm_if_pending(void);

bool pps_time_compute(uint64_t event_us, uint32_t *out_sec, uint32_t *out_frac_us);

bool pps_time_is_valid(void);

bool pps_time_fix_ok(void);

uint32_t pps_time_age_ms(void);

uint32_t pps_time_last_epoch(void);

void pps_time_health_check(void);

uint32_t pps_time_pps_count(void);
