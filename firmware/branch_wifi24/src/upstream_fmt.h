#pragma once

#include "branch_defs.h"
#include "dedup.h"
#include "wids.h"

void upstream_init(void);

void upstream_send_line(const char *line, size_t len);

void upstream_emit_wa(const dedup_entry_t *e, uint32_t epoch_s, uint32_t frac_us, bool time_valid);

void upstream_emit_wp(const wids_event_t *pr, uint32_t epoch_s, uint32_t frac_us, bool time_valid);

void upstream_emit_et(const evil_twin_alert_t *al, uint32_t epoch_s, uint32_t frac_us, bool time_valid);

void upstream_emit_df(const deauth_alert_t *al, uint32_t epoch_s, uint32_t frac_us, bool time_valid);

void upstream_emit_bs(uint32_t uptime_s, bool time_valid, bool fix_ok, uint32_t pps_age_ms,
                      uint16_t dedup_count, uint32_t err_count);
