#pragma once

#include "ble_defs.h"

/* NMEA-style framing identical to wifi24_leaf_protocol_v1_1.md §2, in plain C
 * over the ESP-IDF UART driver. */

void uart_proto_init(void);

uint8_t uart_proto_xor_checksum(const char *data, size_t len);

/* Append '*XX\n' to a NUL-terminated body that starts with '$'. */
bool uart_proto_append_checksum(char *line, size_t cap);

/* Frame `body` (a '$'-prefixed string without checksum) and send it. */
bool uart_proto_send_framed(const char *body);

bool uart_proto_validate_checksum(const char *line, size_t len);

int uart_proto_split_fields(char *line_body, char **fields, int max_fields);

size_t uart_proto_hex_encode(const uint8_t *bytes, size_t n, char *out, size_t cap);

bool uart_proto_format_mac(const uint8_t mac[6], char *out, size_t cap);

/* Read available bytes from the BC link, assembling complete lines. Returns
 * true and fills `out` (NUL-terminated, length in *out_len) when a line is
 * ready; false otherwise. Non-blocking. */
bool uart_proto_poll_line(char *out, size_t cap, size_t *out_len);
