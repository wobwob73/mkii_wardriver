#pragma once

#include "leaf_defs.h"

namespace cfg {

void init();

bool adopt_from_cf(const char *leaf_id, LeafMode mode, ChannelSetId set_id,
                   uint16_t dwell_ms);

bool set_channel_mask_lo_hi(uint16_t lo, uint16_t hi);

bool adopted();
const LeafConfig &state();
const char *id_str();
LeafMode mode();
ChannelSetId channel_set();
uint16_t dwell_ms();
uint32_t channel_mask();

void apply_defaults();

}
