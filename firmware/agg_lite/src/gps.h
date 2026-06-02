#pragma once

#include "agg_defs.h"

/* GPS NMEA reader. Logic ported from stm32_h753_firmware_v1_0.md §4 (RMC/GGA
 * parse, NMEA checksum + field-range validation before any timebase update,
 * F-003); transport changed from I2C to UART1. On a valid RMC second the whole
 * UTC second is fed to pps_time_apply_from_gps(), associating it with the most
 * recent 1PPS edge. */

typedef struct {
    uint32_t epoch_s;
    bool     time_valid;
    bool     fix_ok;
    int32_t  lat_e7;
    int32_t  lon_e7;
    int16_t  alt_cm;
    uint8_t  sat_count;
    uint8_t  hdop_x10;
} gps_state_t;

void gps_init(void);

/* No-op in v1.0.x (module pre-configured via u-center); §5 / open item 6. */
void gps_push_config(void);

void gps_poll(void);

const gps_state_t *gps_state_get(void);

uint32_t gps_bytes_read_count(void);

uint32_t gps_nmea_msg_count(void);
