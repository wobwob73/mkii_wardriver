#pragma once

#include "leaf_defs.h"

namespace wifi_scan {

void init();

/* Parked single-channel passive scan (mode 0). */
void start_async(uint8_t channel);

bool busy();

bool poll_and_emit();

void process();

/* --- Scan-Hop (mode 2, v1.3 §6A) --------------------------------------- */
/* Build the hop set from a 14-bit channel mask and set the per-channel dwell.
 * Called on $CF (mode=2) and at standalone fallback. */
void configure_scanhop(uint16_t mask, uint16_t dwell_ms);

/* Live $CH re-derivation: finish the current channel, rebuild the hop set,
 * reset the cursor to start a fresh sweep (§6A.3). */
void apply_scanhop_mask(uint16_t mask);

/* Drive one Scan-Hop step: emit $AP per channel as results arrive, one $BK per
 * full sweep of the hop set. */
void process_scanhop();

uint32_t consecutive_failures();

}
