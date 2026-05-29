#pragma once

#include "env_defs.h"

void pps_time_init(void);
void pps_time_isr(unsigned int gpio, uint32_t events);
void pps_time_apply_tm(uint32_t epoch_s, bool fix_ok);
bool pps_time_compute(uint64_t event_us, uint32_t *out_sec, uint32_t *out_frac_us);
bool pps_time_is_valid(void);
bool pps_time_fix_ok(void);
uint32_t pps_time_age_ms(void);
void pps_time_health_check(void);
