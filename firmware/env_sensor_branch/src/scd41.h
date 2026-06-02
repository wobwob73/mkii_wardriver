#pragma once

#include "env_defs.h"

bool scd41_init(void);
bool scd41_data_ready(bool *ready);
bool scd41_read(int *co2_ppm, float *temp_c, float *humid_pct);
