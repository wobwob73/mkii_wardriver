#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

namespace uart_proto {

uint8_t xor_checksum(const char *data, size_t len);

bool append_checksum(char *line, size_t cap);

bool send_line(const char *line);

bool send_framed(const char *body);

/* Cumulative TX frames dropped because the bounded 50 ms send_line deadline
 * elapsed before the line could be fully written. Surfaced via $HB. */
uint32_t tx_drop_count();

class LineReceiver {
public:
    LineReceiver();
    void feed(uint8_t byte);
    bool has_line() const { return ready_; }
    const char *line() const { return buf_; }
    size_t length() const { return ready_ ? len_ : 0; }
    void clear() { ready_ = false; }

private:
    char     buf_[MAX_LINE_LEN + 1];
    uint16_t pos_;
    uint16_t len_;
    bool     in_frame_;
    bool     ready_;
};

bool validate_checksum(const char *line, size_t len);

int split_fields(char *line_body, char **fields, int max_fields);

size_t hex_encode(const uint8_t *bytes, size_t n, char *out, size_t cap);

size_t hex_decode(const char *hex, uint8_t *out, size_t cap);

bool format_mac(const uint8_t mac[6], char *out, size_t cap);

bool parse_mac(const char *s, uint8_t mac[6]);

}
