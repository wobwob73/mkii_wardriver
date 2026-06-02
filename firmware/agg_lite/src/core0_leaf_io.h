#pragma once

#include "agg_defs.h"

void core0_init(void);

void core0_run(void);

/* Cumulative malformed/unknown-line count on the Leaf links (checksum + parse
 * failures). Folds into $LA.err_count. */
uint32_t core0_err_count(void);
