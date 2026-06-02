#include "config.h"

#include <string.h>
#include <stdio.h>

namespace cfg {

static LeafConfig g_cfg;

void init() {
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.adopted = false;
    /* v1.3 §9: the firmware-wide fallback is now Scan-Hop, mask 1057, 200 ms. */
    g_cfg.id_str[0] = 'W';
    g_cfg.id_str[1] = 'H';
    g_cfg.id_str[2] = '?';
    g_cfg.id_str[3] = '\0';
    g_cfg.mode = LEAF_MODE_SCANHOP;
    g_cfg.channel = 0;
    g_cfg.dwell_ms = SCANHOP_DEFAULT_DWELL_MS;
    g_cfg.channel_mask = SCANHOP_DEFAULT_MASK;
}

bool adopt_from_cf(const char *leaf_id, LeafMode mode, uint8_t channel,
                   uint16_t param1, uint16_t /*param2*/) {
    if (!valid_leaf_id(leaf_id)) return false;
    /* Mode is enum-coerced from a wire byte (cmd_handler.cpp::handle_cf casts
     * an `int` to LeafMode). Reject anything outside the declared enum so a
     * corrupt or future $CF cannot put the leaf into an unknown state. v1.3
     * widens the accepted set to {0,1,2}. */
    if (mode != LEAF_MODE_SCAN && mode != LEAF_MODE_WIDS &&
        mode != LEAF_MODE_SCANHOP) return false;
    g_cfg.adopted = true;
    /* Copy the full id so 3-char Scan-Hop ids (WH1) survive, not just W1..W4. */
    strncpy(g_cfg.id_str, leaf_id, sizeof(g_cfg.id_str) - 1);
    g_cfg.id_str[sizeof(g_cfg.id_str) - 1] = '\0';
    g_cfg.mode = mode;
    if (mode == LEAF_MODE_SCAN) {
        if (channel >= 1 && channel <= 14) g_cfg.channel = channel;
        else                                g_cfg.channel = 1;
        g_cfg.dwell_ms = 0;
    } else if (mode == LEAF_MODE_WIDS) {
        g_cfg.channel = 0;
        g_cfg.dwell_ms = (param1 == 0) ? 100 : param1;
        g_cfg.channel_mask = 0x3FFF;
    } else { /* LEAF_MODE_SCANHOP */
        /* channel is ignored (hop set comes from the mask); $CF resets the
         * mask to the Scan-Hop default 1057 (1/6/11), overridable via $CH. */
        g_cfg.channel = 0;
        g_cfg.dwell_ms = (param1 == 0) ? SCANHOP_DEFAULT_DWELL_MS : param1;
        g_cfg.channel_mask = SCANHOP_DEFAULT_MASK;
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
    /* v1.3 §5.2: standalone fallback (no $CF within 10 s) is now Scan-Hop with
     * the default mask 1057 (1/6/11) and 200 ms dwell, under leaf_id WH?.
     * Replaces the retired Scan/ch-1 default. In the full Branch the BC always
     * sends an explicit $CF first, which overrides this before any scanning. */
    g_cfg.adopted = true;
    g_cfg.id_str[0] = 'W';
    g_cfg.id_str[1] = 'H';
    g_cfg.id_str[2] = '?';
    g_cfg.id_str[3] = '\0';
    g_cfg.mode = LEAF_MODE_SCANHOP;
    g_cfg.channel = 0;
    g_cfg.dwell_ms = SCANHOP_DEFAULT_DWELL_MS;
    g_cfg.channel_mask = SCANHOP_DEFAULT_MASK;
}

}
