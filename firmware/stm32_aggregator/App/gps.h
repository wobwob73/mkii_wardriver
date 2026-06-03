#pragma once

#include "stm32_config.h"

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

void gps_push_config(void);

void gps_poll(void);

const gps_state_t *gps_state_get(void);

uint32_t gps_bytes_read_count(void);

uint32_t gps_nmea_msg_count(void);
