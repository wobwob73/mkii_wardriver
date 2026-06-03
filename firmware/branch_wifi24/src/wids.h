#pragma once

#include "branch_defs.h"

typedef enum {
    ET_NONE        = 0,
    ET_SSID_MISMATCH = 1,
    ET_ENC_MISMATCH  = 2,
} evil_twin_kind_t;

typedef struct {
    evil_twin_kind_t kind;
    uint8_t  rogue_bssid[6];
    uint8_t  known_bssid[6];
    uint8_t  ssid[32];
    uint8_t  ssid_len;
    uint8_t  rogue_enc;
    uint8_t  known_enc;
    int8_t   rogue_rssi;
    uint8_t  channel;
    uint64_t local_timer_us;
} evil_twin_alert_t;

typedef struct {
    uint8_t  src[6];
    uint8_t  dst[6];
    uint16_t count;
    uint64_t window_start_us;
    uint8_t  channel;
    uint64_t last_event_us;
} deauth_alert_t;

void wids_init(void);

bool wids_check_evil_twin(const wids_event_t *bc, evil_twin_alert_t *out);

bool wids_check_deauth(const wids_event_t *de, deauth_alert_t *out);
