#pragma once

#include "env_defs.h"

bool lis3mdl_init(void);
bool lis3mdl_read(float *mx_uT, float *my_uT, float *mz_uT);
