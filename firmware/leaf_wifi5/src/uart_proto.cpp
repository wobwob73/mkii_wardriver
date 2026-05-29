#include "uart_proto.h"

#include "leaf_defs.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>

namespace uart_proto {

static inline char hex_nibble(uint8_t n) {
    return (n < 10) ? ('0' + n) : ('A' + (n - 10));
}

static inline int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

uint8_t xor_checksum(const char *data, size_t len) {
    uint8_t cs = 0;
    for (size_t i = 0; i < len; i++) cs ^= (uint8_t)data[i];
    return cs;
}

bool append_checksum(char *line, size_t cap) {
    if (!line) return false;
    size_t n = strlen(line);
    if (n < 2 || line[0] != '$') return false;
    if (n + 4 >= cap) return false;
    uint8_t cs = xor_checksum(line + 1, n - 1);
    line[n] = '*';
    line[n + 1] = hex_nibble((cs >> 4) & 0x0F);
    line[n + 2] = hex_nibble(cs & 0x0F);
    line[n + 3] = '\n';
    line[n + 4] = '\0';
    return true;
}

bool send_line(const char *line) {
    if (!line) return false;
    size_t n = strlen(line);
    if (n == 0) return false;
    while (Serial.availableForWrite() < (int)n) {
        delay(0);
    }
    Serial.write((const uint8_t *)line, n);
    return true;
}

bool send_framed(const char *body) {
    if (!body) return false;
    char line[MAX_LINE_LEN + 1];
    size_t n = strlen(body);
    if (n + 4 >= sizeof(line)) return false;
    memcpy(line, body, n + 1);
    if (!append_checksum(line, sizeof(line))) return false;
    return send_line(line);
}

LineReceiver::LineReceiver()
    : pos_(0), len_(0), in_frame_(false), ready_(false) {
    buf_[0] = '\0';
}

void LineReceiver::feed(uint8_t byte) {
    if (ready_) return;

    if (byte == '$') {
        in_frame_ = true;
        pos_ = 0;
        buf_[pos_++] = '$';
        return;
    }

    if (!in_frame_) return;

    if (byte == '\n') {
        if (pos_ < MAX_LINE_LEN) {
            buf_[pos_] = '\0';
            len_ = pos_;
            ready_ = true;
        } else {
            pos_ = 0;
            in_frame_ = false;
        }
        return;
    }

    if (pos_ >= MAX_LINE_LEN) {
        pos_ = 0;
        in_frame_ = false;
        return;
    }

    buf_[pos_++] = (char)byte;
}

bool validate_checksum(const char *line, size_t len) {
    if (!line || len < 5) return false;
    if (line[0] != '$') return false;
    const char *star = nullptr;
    for (size_t i = 1; i < len; i++) {
        if (line[i] == '*') { star = line + i; break; }
    }
    if (!star) return false;
    if ((size_t)((star + 3) - line) > len) return false;
    int hi = hex_to_int(star[1]);
    int lo = hex_to_int(star[2]);
    if (hi < 0 || lo < 0) return false;
    uint8_t expected = (uint8_t)((hi << 4) | lo);
    uint8_t actual = xor_checksum(line + 1, (size_t)(star - line - 1));
    return expected == actual;
}

int split_fields(char *line_body, char **fields, int max_fields) {
    if (!line_body || !fields || max_fields <= 0) return 0;
    int n = 0;
    fields[n++] = line_body;
    for (char *p = line_body; *p && n < max_fields; p++) {
        if (*p == ',') {
            *p = '\0';
            fields[n++] = p + 1;
        }
    }
    return n;
}

size_t hex_encode(const uint8_t *bytes, size_t n, char *out, size_t cap) {
    if (!out || cap == 0) return 0;
    if (cap < 2 * n + 1) return 0;
    for (size_t i = 0; i < n; i++) {
        out[2 * i]     = hex_nibble((bytes[i] >> 4) & 0x0F);
        out[2 * i + 1] = hex_nibble(bytes[i] & 0x0F);
    }
    out[2 * n] = '\0';
    return 2 * n;
}

size_t hex_decode(const char *hex, uint8_t *out, size_t cap) {
    if (!hex || !out) return 0;
    size_t hlen = strlen(hex);
    if ((hlen & 1) != 0) return 0;
    size_t blen = hlen / 2;
    if (blen > cap) return 0;
    for (size_t i = 0; i < blen; i++) {
        int hi = hex_to_int(hex[2 * i]);
        int lo = hex_to_int(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return 0;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return blen;
}

bool format_mac(const uint8_t mac[6], char *out, size_t cap) {
    if (!mac || !out || cap < 18) return false;
    snprintf(out, cap, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

bool parse_mac(const char *s, uint8_t mac[6]) {
    if (!s || !mac) return false;
    if (strlen(s) != 17) return false;
    for (int i = 0; i < 6; i++) {
        if (i > 0 && s[3 * i - 1] != ':') return false;
        int hi = hex_to_int(s[3 * i]);
        int lo = hex_to_int(s[3 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        mac[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

}
