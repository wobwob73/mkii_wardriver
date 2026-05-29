#pragma once

#include "leaf_defs.h"
#include <stdint.h>

namespace bssid_tracker {

void init();

bool seen_or_insert(const uint8_t bssid[6]);

void reset_cycle();

uint32_t saturation_count();

}
