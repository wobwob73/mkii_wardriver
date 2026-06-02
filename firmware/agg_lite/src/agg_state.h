#pragma once

#include "agg_defs.h"

/*
 * Tiny cross-core coordination flag. The §10 state machine gates Leaf
 * configuration (INIT_LEAVES) until Core 1 has established time (and the GPS
 * fix gate is handled by sd_log's pending bucket). Core 1 raises the request;
 * Core 0 sends the per-slot $CF once it sees it. A single bool with a memory
 * barrier is sufficient — it is set once and never cleared.
 */

void agg_request_leaf_init(void);

bool agg_leaf_init_requested(void);
