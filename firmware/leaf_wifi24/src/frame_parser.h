#pragma once

#include "leaf_defs.h"
#include <stdint.h>
#include <stddef.h>

namespace frame_parser {

struct MgmtAddrs {
    uint8_t dst[6];
    uint8_t src[6];
    uint8_t bssid[6];
};

bool decode_mgmt_addrs(const uint8_t *frame, size_t len, MgmtAddrs &out);

bool decode_reason_code(const uint8_t *frame, size_t len, uint16_t &reason);

bool parse_beacon(const uint8_t *frame, size_t len,
                  uint8_t ssid[32], uint8_t &ssid_len,
                  uint8_t &enc, uint8_t &hidden);

bool parse_probe_request(const uint8_t *frame, size_t len,
                         uint8_t ssid[32], uint8_t &ssid_len);

uint8_t frame_subtype(const uint8_t *frame, size_t len);

}
