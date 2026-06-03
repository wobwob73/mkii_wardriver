#pragma once

#include "env_defs.h"

bool bmp390_init(void);
bool bmp390_read(float *press_hpa, float *alt_m, float *temp_c);
