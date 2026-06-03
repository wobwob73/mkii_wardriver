#pragma once

#include "env_defs.h"

bool sgp41_init(void);

bool sgp41_start_conditioning(void);
bool sgp41_finish_conditioning(void);

bool sgp41_measure_raw(float humid_pct, float temp_c, uint16_t *raw_voc, uint16_t *raw_nox);

void sgp41_run_gas_index(uint16_t raw_voc, uint16_t raw_nox, int *voc_index, int *nox_index);
