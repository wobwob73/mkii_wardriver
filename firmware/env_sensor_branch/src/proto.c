#include "proto.h"

#include <string.h>

static inline char hex_nibble(uint8_t n) {
    return (n < 10) ? ('0' + n) : ('A' + (n - 10));
}

static inline int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

uint8_t proto_xor_checksum(const char *data, size_t len) {
    uint8_t cs = 0;
    for (size_t i = 0; i < len; i++) cs ^= (uint8_t)data[i];
    return cs;
}

bool proto_validate_line(const char *line, size_t len) {
    if (!line || len < 5) return false;
    if (line[0] != '$') return false;
    const char *star = NULL;
    for (size_t i = 1; i < len; i++) {
        if (line[i] == '*') { star = line + i; break; }
    }
    if (!star) return false;
    if ((size_t)((star + 3) - line) > len) return false;
    int hi = hex_to_int(star[1]);
    int lo = hex_to_int(star[2]);
    if (hi < 0 || lo < 0) return false;
    uint8_t expected = (uint8_t)((hi << 4) | lo);
    uint8_t actual = proto_xor_checksum(line + 1, (size_t)(star - line - 1));
    return expected == actual;
}

int proto_split_body(char *body, char **fields, int max_fields) {
    if (!body || !fields || max_fields <= 0) return 0;
    int n = 0;
    fields[n++] = body;
    for (char *p = body; *p && n < max_fields; p++) {
        if (*p == ',') {
            *p = '\0';
            fields[n++] = p + 1;
        }
    }
    return n;
}

bool proto_finalize_line(char *line, size_t cap) {
    if (!line) return false;
    size_t n = strlen(line);
    if (n < 2 || line[0] != '$') return false;
    if (n + 4 >= cap) return false;
    uint8_t cs = proto_xor_checksum(line + 1, n - 1);
    line[n]     = '*';
    line[n + 1] = hex_nibble((cs >> 4) & 0x0F);
    line[n + 2] = hex_nibble(cs & 0x0F);
    line[n + 3] = '\n';
    line[n + 4] = '\0';
    return true;
}

void proto_lr_init(line_receiver_t *lr) {
    if (!lr) return;
    lr->pos = 0;
    lr->len = 0;
    lr->in_frame = false;
    lr->ready = false;
    lr->buf[0] = '\0';
}

bool proto_lr_has_line(const line_receiver_t *lr) {
    return lr && lr->ready;
}

void proto_lr_clear(line_receiver_t *lr) {
    if (!lr) return;
    lr->ready = false;
    lr->pos = 0;
    lr->in_frame = false;
}

void proto_lr_feed(line_receiver_t *lr, uint8_t b) {
    if (!lr || lr->ready) return;

    if (b == '$') {
        lr->in_frame = true;
        lr->pos = 0;
        lr->buf[lr->pos++] = '$';
        return;
    }
    if (!lr->in_frame) return;

    if (b == '\n') {
        if (lr->pos < MAX_LINE_LEN) {
            lr->buf[lr->pos] = '\0';
            lr->len = lr->pos;
            lr->ready = true;
        } else {
            lr->pos = 0;
            lr->in_frame = false;
        }
        return;
    }

    if (lr->pos >= MAX_LINE_LEN) {
        lr->pos = 0;
        lr->in_frame = false;
        return;
    }
    lr->buf[lr->pos++] = (char)b;
}
