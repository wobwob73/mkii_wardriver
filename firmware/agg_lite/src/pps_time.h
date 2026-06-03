#pragma once

#include "agg_defs.h"

/*
 * 1PPS / time discipline. In the multi-box system the BC consumed $TM over its
 * upstream UART; here the aggregator IS the time authority — it captures its
 * own GPS 1PPS edge (GP10) and, once the GPS NMEA UTC second is in hand,
 * generates $TM internally (pps_time_apply_from_gps). No $TM is transmitted on
 * any wire; it is purely an internal event feeding all three dedup pipelines.
 *
 * The PPS GPIO ISR runs on Core 0; readers + the apply path run on Core 1.
 * All access is serialized by an internal hardware spinlock (F-004) so the
 * 64-bit pps_timer_us cannot tear and an apply cannot interleave with an edge.
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

/* Called from the GPIO IRQ callback on Core 0. */
void pps_time_isr(unsigned int gpio, uint32_t events);

/* Called from Core 1 when the GPS yields a whole UTC second; applies to the
 * last pending edge (no-op if no edge is currently pending). This is the
 * local $TM generator that replaces the STM32's $TM emission. */
void pps_time_apply_from_gps(uint32_t epoch_s, bool fix_ok);

bool pps_time_snapshot(pps_snapshot_t *out);

bool pps_time_compute(uint64_t event_us, uint32_t *out_sec, uint32_t *out_frac_us);

bool pps_time_is_valid(void);

bool pps_time_fix_ok(void);

uint32_t pps_time_age_ms(void);

uint32_t pps_time_last_epoch(void);

void pps_time_health_check(void);
