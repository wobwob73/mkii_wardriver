#pragma once

#include "branch_defs.h"

uint8_t proto_xor_checksum(const char *data, size_t len);

bool proto_validate_line(const char *line, size_t len);

int proto_split_body(char *body, char **fields, int max_fields);

size_t proto_hex_encode(const uint8_t *bytes, size_t n, char *out, size_t cap);

size_t proto_hex_decode(const char *hex, uint8_t *out, size_t cap);

bool proto_format_mac(const uint8_t mac[6], char *out, size_t cap);

bool proto_parse_mac(const char *s, uint8_t mac[6]);

bool proto_finalize_line(char *line, size_t cap);

bool proto_validate_inner(const char *inner, size_t len);

typedef struct {
    char     buf[MAX_LINE_LEN + 1];
    uint16_t pos;
    uint16_t len;
    bool     in_frame;
    bool     ready;
} line_receiver_t;

void proto_lr_init(line_receiver_t *lr);
void proto_lr_feed(line_receiver_t *lr, uint8_t b);
bool proto_lr_has_line(const line_receiver_t *lr);
void proto_lr_clear(line_receiver_t *lr);
