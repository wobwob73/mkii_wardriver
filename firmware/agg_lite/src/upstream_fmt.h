#pragma once

#include "agg_defs.h"

/*
 * Upstream record formatting. The aggregator produces the IDENTICAL schemas
 * the multi-box system produces (§7), so an Analyzer capture is byte-compatible
 * regardless of topology — only the producer changes. Records are handed to the
 * sink (SD ring + CDC mirror) via record_sink_write(); there is no upstream
 * UART here.
 *
 *   $WA   branch_controller_wifi24_v1_0.md §7.1 (W24) / wifi5_branch §10 (W5G)
 *   $BD   blebt_branch_v1_0.md §10.1
 *   $BX   blebt_branch_v1_0.md §10.2 (pass-through)
 *   $LA   lite_aggregator_v1_0.md §7.5 (consolidated heartbeat, 23 fields)
 *
 * Not produced in light-duty v1.0: $ET / $DF (no WIDS Leaves), $BC_T (no BT
 * Classic), $WP (no promiscuous probe source) — §6.1/§6.3, §7.
 */

/* Implemented by core1_main.c — fans a framed line out to SD + CDC. */
void record_sink_write(const char *line, size_t len);

typedef struct {
    bool     have_time;     /* false → timestamp printed as 0 (pre-fix) */
    uint32_t epoch_s;
    uint32_t frac_us;
    uint32_t uptime_s;
    int      mode;          /* 0 = Standalone, 1 = Connected */
    int      time_valid;
    int      fix_ok;
    int      sat_count;
    int32_t  lat_e7;
    int32_t  lon_e7;
    int16_t  alt_cm;
    uint32_t pps_age_ms;
    int      w24_st;
    int      w5g_st;
    int      ble_st;
    int      sd_ok;
    uint32_t sd_kb;
    int      q_w24;
    int      q_w5g;
    int      q_ble;
    int      dedup_w24;
    int      dedup_w5g;
    int      dedup_ble;
    uint32_t err_count;
} la_fields_t;

void upstream_emit_wa(uint8_t slot, const dedup_entry_t *e,
                      uint32_t epoch_s, uint32_t frac_us, bool time_valid);

void upstream_emit_bd(const ble_dedup_entry_t *e,
                      uint32_t epoch_s, uint32_t frac_us, bool time_valid);

void upstream_emit_bx(const ble_ext_t *bx,
                      uint32_t epoch_s, uint32_t frac_us, bool time_valid);

void upstream_emit_la(const la_fields_t *f);
