#pragma once

#include "env_defs.h"

/*
 * PPS / time state. The PPS GPIO ISR runs on core0; readers and the $TM-apply
 * path run on core1. Reads and writes therefore must NOT touch the underlying
 * state directly — both go through pps_time_snapshot() (readers) or the
 * dedicated apply/isr/health entry points (writers), all serialized by an
 * internal hardware spinlock so a 64-bit pps_timer_us cannot tear and an
 * apply cannot interleave with an ISR fire.
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
void pps_time_isr(unsigned int gpio, uint32_t events);
void pps_time_apply_tm(uint32_t epoch_s, bool fix_ok);
bool pps_time_snapshot(pps_snapshot_t *out);
bool pps_time_compute(uint64_t event_us, uint32_t *out_sec, uint32_t *out_frac_us);
bool pps_time_is_valid(void);
bool pps_time_fix_ok(void);
uint32_t pps_time_age_ms(void);
void pps_time_health_check(void);
