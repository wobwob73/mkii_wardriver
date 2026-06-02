#pragma once

#include "agg_defs.h"

/*
 * SD record log. Logic ported from stm32_h753_firmware_v1_0.md §8, over the
 * SPI-FatFs backend (sd_spi_fatfs.h) instead of the SDMMC HAL.
 *
 * Records ($WA/$WP/$BD/$BX + $LA) are enqueued into a RAM ring by Core 1's
 * format stage; a drain step writes them to the card in batches so a slow-card
 * write stall never blocks ingest (§8.2). All calls run on Core 1.
 *
 * File layout (§8.1):  /<session_id>/records.log
 * session_id = YYYYMMDD_HHMMSS of the first valid GPS fix; until then records
 * go to a "pending" bucket and the directory is renamed at first fix.
 */

void sd_log_init(void);

bool sd_log_mounted(void);

/* True once the directory has been renamed from "pending" to a fix-stamped id. */
bool sd_log_has_session(void);

/* Rename the pending bucket to a YYYYMMDD_HHMMSS session and reopen the log. */
void sd_log_set_session(const char *session_id);

/* Enqueue a fully framed line (including trailing '\n') into the RAM ring.
 * On ring-full the line is dropped and the drop counter advances. */
void sd_log_enqueue(const char *line, size_t len);

/* Drain the ring to the card in batches; periodic f_sync; background remount. */
void sd_log_tick(void);

uint32_t sd_log_kb_written(void);
uint32_t sd_log_drop_count(void);
uint32_t sd_log_error_count(void);
uint16_t sd_log_ring_high_water(void);
