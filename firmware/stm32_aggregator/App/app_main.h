#pragma once

#include "stm32_config.h"

void app_main(void);

void app_on_pps_edge(uint64_t timer_us);
