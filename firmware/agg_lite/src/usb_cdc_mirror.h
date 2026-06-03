#pragma once

#include "agg_defs.h"

/*
 * Optional live mirror over native USB CDC (§2). When a host is connected the
 * aggregator is in "Connected" mode and every record is mirrored to CDC as it
 * is produced; SD remains the durable sink. With no host it is "Standalone"
 * and CDC writes are no-ops. The mirror never blocks ingest — a write to a
 * disconnected/!writable CDC is dropped, not stalled.
 */

void usb_cdc_mirror_init(void);

bool usb_cdc_mirror_connected(void);

void usb_cdc_mirror_write(const char *line, size_t len);
