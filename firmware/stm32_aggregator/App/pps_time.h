#pragma once

#include "stm32_config.h"

/*
 * PPS / time state. pps_time_on_edge() is called from the EXTI ISR;
 * pps_time_apply_from_gps() and the readers are called from the main loop.
 * All accessors briefly mask interrupts (PRIMASK) so the 64-bit pps_timer_us
 * cannot tear between the ISR write and a main-loop read, and so apply
 * cannot interleave with an ISR fire. The pps_state_t internals are
 * private — readers use pps_time_snapshot().
 */

typedef struct {
    uint64_t pps_timer_us;
    uint32_t epoch_s;
    uint32_t pps_count;
    bool     time_valid;
    bool     fix_ok;
    bool     pps_pending;
} pps_snapshot_t;

void pps_time_init(void);

void pps_time_on_edge(uint64_t timer_us);

void pps_time_apply_from_gps(uint32_t epoch_s, bool fix_ok);

bool pps_time_emit_tm_if_pending(void);

bool pps_time_snapshot(pps_snapshot_t *out);

bool pps_time_compute(uint64_t event_us, uint32_t *out_sec, uint32_t *out_frac_us);

bool pps_time_is_valid(void);

bool pps_time_fix_ok(void);

uint32_t pps_time_age_ms(void);

uint32_t pps_time_last_epoch(void);

void pps_time_health_check(void);

uint32_t pps_time_pps_count(void);
