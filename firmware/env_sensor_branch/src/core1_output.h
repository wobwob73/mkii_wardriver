#pragma once

#include "env_defs.h"

void core1_output_init(bool imu_ok, bool mag_ok, bool baro_ok, bool scd_ok, bool sgp_ok);

void core1_output_run(void);
