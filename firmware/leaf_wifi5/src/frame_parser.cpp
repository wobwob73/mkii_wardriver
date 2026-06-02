#include "frame_parser.h"

#include <string.h>

namespace frame_parser {

static const size_t MGMT_HDR_LEN = 24;
static const size_t BEACON_FIXED = 12;
static const size_t BEACON_BODY_OFFSET = MGMT_HDR_LEN + BEACON_FIXED;
static const size_t PROBE_REQ_BODY_OFFSET = MGMT_HDR_LEN;

static bool valid_mgmt(const uint8_t *frame, size_t len) {
    if (!frame || len < MGMT_HDR_LEN) return false;
    uint8_t type = (frame[0] >> 2) & 0x03;
    return type == 0;
}

uint8_t frame_subtype(const uint8_t *frame, size_t len) {
    if (!frame || len < 2) return 0xFF;
    return (frame[0] & 0xF0);
}

bool decode_mgmt_addrs(const uint8_t *frame, size_t len, MgmtAddrs &out) {
    if (!valid_mgmt(frame, len)) return false;
    memcpy(out.dst,   frame + 4,  6);
    memcpy(out.src,   frame + 10, 6);
    memcpy(out.bssid, frame + 16, 6);
    return true;
}

bool decode_reason_code(const uint8_t *frame, size_t len, uint16_t &reason) {
    if (!valid_mgmt(frame, len)) return false;
    if (len < MGMT_HDR_LEN + 2) return false;
    reason = (uint16_t)frame[MGMT_HDR_LEN] | ((uint16_t)frame[MGMT_HDR_LEN + 1] << 8);
    return true;
}

static bool walk_ies(const uint8_t *ie_start, const uint8_t *frame_end,
                     uint8_t ssid[32], uint8_t &ssid_len,
                     bool &has_rsn, uint32_t &rsn_akm_mask, uint8_t &rsn_caps_lo,
                     bool &has_wpa) {
    ssid_len = 0;
    has_rsn = false;
    rsn_akm_mask = 0;
    rsn_caps_lo = 0;
    has_wpa = false;

    const uint8_t *p = ie_start;
    while (p + 2 <= frame_end) {
        uint8_t tag = p[0];
        uint8_t length = p[1];
        const uint8_t *value = p + 2;
        if (value + length > frame_end) return false;

        if (tag == 0) {
            ssid_len = length;
            if (ssid_len > 32) ssid_len = 32;
            if (ssid_len > 0) memcpy(ssid, value, ssid_len);
        } else if (tag == 48 && length >= 4) {
            has_rsn = true;
            const uint8_t *q = value;
            const uint8_t *end = value + length;
            q += 2;
            if (q + 4 > end) goto next_ie;
            q += 4;
            if (q + 2 > end) goto next_ie;
            uint16_t pcount = (uint16_t)q[0] | ((uint16_t)q[1] << 8);
            q += 2;
            if (q + 4 * pcount > end) goto next_ie;
            q += 4 * pcount;
            if (q + 2 > end) goto next_ie;
            uint16_t acount = (uint16_t)q[0] | ((uint16_t)q[1] << 8);
            q += 2;
            for (uint16_t i = 0; i < acount; i++) {
                if (q + 4 > end) break;
                uint8_t akm = q[3];
                if (akm < 32) rsn_akm_mask |= (uint32_t)(1u << akm);
                q += 4;
            }
            if (q + 2 <= end) {
                rsn_caps_lo = q[0];
            }
        } else if (tag == 221 && length >= 8) {
            if (value[0] == 0x00 && value[1] == 0x50 && value[2] == 0xF2 && value[3] == 0x01) {
                has_wpa = true;
            }
        }
    next_ie:
        p = value + length;
    }
    return true;
}

static uint8_t enc_from_rsn(bool has_rsn, uint32_t akm_mask, uint8_t rsn_caps_lo,
                            bool has_wpa, bool privacy_bit) {
    bool akm1   = (akm_mask & (1u << 1))  != 0;
    bool akm2   = (akm_mask & (1u << 2))  != 0;
    bool akm3   = (akm_mask & (1u << 3))  != 0;
    bool akm4   = (akm_mask & (1u << 4))  != 0;
    bool akm5   = (akm_mask & (1u << 5))  != 0;
    bool akm6   = (akm_mask & (1u << 6))  != 0;
    bool akm8   = (akm_mask & (1u << 8))  != 0;
    bool akm11  = (akm_mask & (1u << 11)) != 0;
    bool akm12  = (akm_mask & (1u << 12)) != 0;
    bool akm18  = (akm_mask & (1u << 18)) != 0;
    bool mfpr   = (rsn_caps_lo & 0x40) != 0;

    if (has_rsn) {
        if (akm18 && !akm1 && !akm2 && !akm3 && !akm4 && !akm5 &&
            !akm6 && !akm8 && !akm11 && !akm12) {
            return LE_OWE;
        }
        if (akm8 && (akm2 || akm6)) return LE_WPA2_WPA3_PSK;
        if (akm8) return LE_WPA3_PSK;
        if (akm5 || akm11 || akm12 || ((akm1 || akm3) && mfpr)) return LE_WPA3_ENT;
        if (akm1 || akm3) return LE_WPA2_ENT;
        if (akm2 || akm4 || akm6) {
            return has_wpa ? LE_WPA_WPA2_PSK : LE_WPA2_PSK;
        }
        if (akm18) return LE_OWE;
        return LE_UNKNOWN;
    }
    if (has_wpa) return LE_WPA_PSK;
    if (privacy_bit) return LE_WEP;
    return LE_OPEN;
}

bool parse_beacon(const uint8_t *frame, size_t len,
                  uint8_t ssid[32], uint8_t &ssid_len,
                  uint8_t &enc, uint8_t &hidden) {
    ssid_len = 0;
    enc = LE_UNKNOWN;
    hidden = 0;
    if (!valid_mgmt(frame, len)) return false;
    if (len < BEACON_BODY_OFFSET) return false;

    bool privacy_bit = (frame[MGMT_HDR_LEN + 10] & 0x10) != 0;

    bool has_rsn = false, has_wpa = false;
    uint32_t akm_mask = 0;
    uint8_t rsn_caps_lo = 0;
    if (!walk_ies(frame + BEACON_BODY_OFFSET, frame + len,
                  ssid, ssid_len, has_rsn, akm_mask, rsn_caps_lo, has_wpa)) {
        return false;
    }
    hidden = (ssid_len == 0) ? 1 : 0;
    enc = enc_from_rsn(has_rsn, akm_mask, rsn_caps_lo, has_wpa, privacy_bit);
    return true;
}

bool parse_probe_request(const uint8_t *frame, size_t len,
                         uint8_t ssid[32], uint8_t &ssid_len) {
    ssid_len = 0;
    if (!valid_mgmt(frame, len)) return false;
    if (len < PROBE_REQ_BODY_OFFSET + 2) return false;

    const uint8_t *p = frame + PROBE_REQ_BODY_OFFSET;
    const uint8_t *end = frame + len;
    while (p + 2 <= end) {
        uint8_t tag = p[0];
        uint8_t length = p[1];
        const uint8_t *value = p + 2;
        if (value + length > end) return false;
        if (tag == 0) {
            ssid_len = length;
            if (ssid_len > 32) ssid_len = 32;
            if (ssid_len > 0) memcpy(ssid, value, ssid_len);
            return true;
        }
        p = value + length;
    }
    return true;
}

}
