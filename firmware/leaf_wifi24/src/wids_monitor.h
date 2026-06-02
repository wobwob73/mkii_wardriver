#pragma once

#include "leaf_defs.h"

namespace wids_monitor {

void init();

void start();

void stop();

void apply_channel_mask(uint16_t mask);

void process();

uint32_t dropped_count();

}
