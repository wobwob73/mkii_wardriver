#pragma once

#include "leaf_defs.h"

namespace wifi_scan {

void init();

void start_async(uint8_t channel);

bool busy();

bool poll_and_emit();

void process();

uint32_t consecutive_failures();

}
