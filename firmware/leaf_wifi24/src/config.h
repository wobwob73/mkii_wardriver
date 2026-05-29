#pragma once

#include "leaf_defs.h"

namespace cfg {

void init();

bool adopt_from_cf(const char *leaf_id, LeafMode mode, uint8_t channel,
                   uint16_t param1, uint16_t param2);

bool set_channel_mask(uint16_t mask);

bool adopted();

const LeafConfig &state();

const char *id_str();

LeafMode mode();

uint8_t channel();

uint16_t dwell_ms();

uint16_t channel_mask();

void apply_defaults();

}
