#pragma once

#include "agg_defs.h"

/*
 * SD backend interface — the seam between the portable session/ring logic in
 * sd_log.c and the physical FatFs-over-SPI card driver.
 *
 * lite_aggregator_v1_0.md §9 lists sd_spi_fatfs.{h,c} as the one genuinely NEW
 * subsystem (a [NEW vendored] FatFs-over-SPI glue, e.g. carlk3's
 * no-OS-FatFS-SD-SPI-RPi-Pico), and §14 open item 5 flags "SPI-FatFs library
 * selection + SD write-stall headroom" as Implementation-phase work.
 *
 * This header is the stable interface; sd_spi_fatfs.c provides the backend.
 * The default build links a documented PLACEHOLDER backend so agg_lite.uf2
 * compiles in CI and the ring/session state machine is exercised end-to-end on
 * the CDC mirror — exactly the pattern used elsewhere in this repo (the STM32
 * PAL stub for host-smoke, gps_push_config() no-op, the SGP41 gas-index stub).
 * Dropping the vendored library into third_party/pico_fatfs_spi/ and selecting
 * -DAGG_SD_BACKEND=fatfs swaps in real, durable writes WITHOUT touching sd_log
 * or anything above it. Until then, the placeholder is NOT a durable sink.
 */

bool sd_be_init(void);

bool sd_be_present(void);

bool sd_be_mount(void);

void sd_be_unmount(void);

bool sd_be_mkdir_p(const char *path);

bool sd_be_rename(const char *from, const char *to);

/* One append-mode logfile handle is tracked internally (open once, write many,
 * reopen on FR_DISK_ERR — §8.2). */
bool sd_be_open_append(const char *path);

/* Returns bytes written (>=0) or -1 on error (e.g. FR_DISK_ERR / hot-remove). */
int sd_be_write(const uint8_t *data, size_t len);

bool sd_be_sync(void);

void sd_be_close(void);

/* True while the backend is mounted and writes are succeeding. */
bool sd_be_ok(void);
