#include "config.h"

#include <string.h>

namespace cfg {

static LeafConfig g_cfg;

static uint16_t default_dwell_for(LeafMode mode) {
    return (mode == LEAF_MODE_WIDS) ? 100 : 200;
}

void init() {
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.adopted = false;
    g_cfg.id_str[0] = 'W';
    g_cfg.id_str[1] = '5';
    g_cfg.id_str[2] = '_';
    g_cfg.id_str[3] = '?';
    g_cfg.id_str[4] = '\0';
    g_cfg.mode = LEAF_MODE_SCAN;
    g_cfg.channel_set = CSID_UNII1_2A;
    g_cfg.dwell_ms = 200;
    g_cfg.channel_mask = mask_for_channel_set(CSID_UNII1_2A);
}

bool adopt_from_cf(const char *leaf_id, LeafMode mode, ChannelSetId set_id,
                   uint16_t dwell_ms) {
    if (!valid_leaf_id(leaf_id)) return false;
    if (set_id < CSID_UNII1 || set_id > CSID_ALL_5G) return false;

    g_cfg.adopted = true;
    memcpy(g_cfg.id_str, leaf_id, 4);
    g_cfg.id_str[4] = '\0';
    g_cfg.mode = mode;
    g_cfg.channel_set = set_id;
    g_cfg.dwell_ms = (dwell_ms == 0) ? default_dwell_for(mode) : dwell_ms;
    g_cfg.channel_mask = mask_for_channel_set(set_id);
    return true;
}

bool set_channel_mask_lo_hi(uint16_t lo, uint16_t hi) {
    uint32_t mask = ((uint32_t)hi << 16) | (uint32_t)lo;
    mask &= 0x01FFFFFFu;
    if (mask == 0) return false;
    g_cfg.channel_mask = mask;
    return true;
}

bool adopted() { return g_cfg.adopted; }
const LeafConfig &state() { return g_cfg; }
const char *id_str() { return g_cfg.id_str; }
LeafMode mode() { return g_cfg.mode; }
ChannelSetId channel_set() { return g_cfg.channel_set; }
uint16_t dwell_ms() { return g_cfg.dwell_ms; }
uint32_t channel_mask() { return g_cfg.channel_mask; }

void apply_defaults() {
    g_cfg.adopted = true;
    g_cfg.id_str[0] = 'W';
    g_cfg.id_str[1] = '5';
    g_cfg.id_str[2] = '_';
    g_cfg.id_str[3] = '?';
    g_cfg.id_str[4] = '\0';
    g_cfg.mode = LEAF_MODE_SCAN;
    g_cfg.channel_set = CSID_UNII1_2A;
    g_cfg.dwell_ms = 200;
    g_cfg.channel_mask = mask_for_channel_set(CSID_UNII1_2A);
}

}
