#include "config.h"

#include <string.h>
#include <stdio.h>

namespace cfg {

static LeafConfig g_cfg;

void init() {
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.adopted = false;
    g_cfg.id_str[0] = 'W';
    g_cfg.id_str[1] = '?';
    g_cfg.id_str[2] = '\0';
    g_cfg.mode = LEAF_MODE_SCAN;
    g_cfg.channel = 1;
    g_cfg.dwell_ms = 100;
    g_cfg.channel_mask = 0x3FFF;
}

bool adopt_from_cf(const char *leaf_id, LeafMode mode, uint8_t channel,
                   uint16_t param1, uint16_t /*param2*/) {
    if (!valid_leaf_id(leaf_id)) return false;
    /* Mode is enum-coerced from a wire byte (cmd_handler.cpp::handle_cf casts
     * an `int` to LeafMode). Reject anything outside the declared enum so a
     * corrupt or future $CF cannot put the leaf into an unknown state. With
     * F-001 fixed, $CF actually reaches W2..W4, so an unvalidated mode byte
     * now matters in practice. */
    if (mode != LEAF_MODE_SCAN && mode != LEAF_MODE_WIDS) return false;
    g_cfg.adopted = true;
    g_cfg.id_str[0] = leaf_id[0];
    g_cfg.id_str[1] = leaf_id[1];
    g_cfg.id_str[2] = '\0';
    g_cfg.mode = mode;
    if (mode == LEAF_MODE_SCAN) {
        if (channel >= 1 && channel <= 14) g_cfg.channel = channel;
        else                                g_cfg.channel = 1;
        g_cfg.dwell_ms = 0;
    } else {
        g_cfg.channel = 0;
        g_cfg.dwell_ms = (param1 == 0) ? 100 : param1;
        g_cfg.channel_mask = 0x3FFF;
    }
    return true;
}

bool set_channel_mask(uint16_t mask) {
    if (mask == 0) return false;
    g_cfg.channel_mask = mask & 0x3FFF;
    return true;
}

bool adopted() { return g_cfg.adopted; }

const LeafConfig &state() { return g_cfg; }

const char *id_str() { return g_cfg.id_str; }

LeafMode mode() { return g_cfg.mode; }

uint8_t channel() { return g_cfg.channel; }

uint16_t dwell_ms() { return g_cfg.dwell_ms; }

uint16_t channel_mask() { return g_cfg.channel_mask; }

void apply_defaults() {
    g_cfg.adopted = true;
    g_cfg.id_str[0] = 'W';
    g_cfg.id_str[1] = '?';
    g_cfg.id_str[2] = '\0';
    g_cfg.mode = LEAF_MODE_SCAN;
    g_cfg.channel = 1;
    g_cfg.dwell_ms = 100;
    g_cfg.channel_mask = 0x3FFF;
}

}
