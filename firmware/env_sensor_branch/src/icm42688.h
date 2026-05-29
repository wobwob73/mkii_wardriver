#pragma once

#include "env_defs.h"

bool icm42688_init(void);
bool icm42688_read(float *ax, float *ay, float *az,
                   float *gx, float *gy, float *gz);
