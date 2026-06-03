#include "uart_proto.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include <string.h>
#include <stdio.h>

#define UART_PORT  ((uart_port_t)LEAF_UART_PORT)

static char     g_rx_buf[MAX_LINE_LEN + 1];
static uint16_t g_rx_pos;
static bool     g_in_frame;

static inline char hex_nibble(uint8_t n) {
    return (n < 10) ? ('0' + n) : ('A' + (n - 10));
}
static inline int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

void uart_proto_init(void) {
    const uart_config_t cfg = {
        .baud_rate = LEAF_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_PORT, 2048, 2048, 0, NULL, 0);
    uart_param_config(UART_PORT, &cfg);
    uart_set_pin(UART_PORT, LEAF_UART_TX_PIN, LEAF_UART_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    g_rx_pos = 0;
    g_in_frame = false;
}

uint8_t uart_proto_xor_checksum(const char *data, size_t len) {
    uint8_t cs = 0;
    for (size_t i = 0; i < len; i++) cs ^= (uint8_t)data[i];
    return cs;
}

bool uart_proto_append_checksum(char *line, size_t cap) {
    if (!line) return false;
    size_t n = strlen(line);
    if (n < 2 || line[0] != '$') return false;
    if (n + 4 >= cap) return false;
    uint8_t cs = uart_proto_xor_checksum(line + 1, n - 1);
    line[n]     = '*';
    line[n + 1] = hex_nibble((cs >> 4) & 0x0F);
    line[n + 2] = hex_nibble(cs & 0x0F);
    line[n + 3] = '\n';
    line[n + 4] = '\0';
    return true;
}

bool uart_proto_send_framed(const char *body) {
    char line[MAX_LINE_LEN + 1];
    size_t n = strlen(body);
    if (n + 4 >= sizeof(line)) return false;
    memcpy(line, body, n + 1);
    if (!uart_proto_append_checksum(line, sizeof(line))) return false;
    int len = (int)strlen(line);
    int wrote = uart_write_bytes(UART_PORT, line, len);
    return wrote == len;
}

bool uart_proto_validate_checksum(const char *line, size_t len) {
    if (!line || len < 5) return false;
    if (line[0] != '$') return false;
    const char *star = NULL;
    for (size_t i = 1; i < len; i++) {
        if (line[i] == '*') { star = line + i; break; }
    }
    if (!star) return false;
    if ((size_t)((star + 3) - line) != len) return false;   /* F-006 */
    int hi = hex_to_int(star[1]);
    int lo = hex_to_int(star[2]);
    if (hi < 0 || lo < 0) return false;
    uint8_t expected = (uint8_t)((hi << 4) | lo);
    uint8_t actual = uart_proto_xor_checksum(line + 1, (size_t)(star - line - 1));
    return expected == actual;
}

int uart_proto_split_fields(char *body, char **fields, int max_fields) {
    if (!body || !fields || max_fields <= 0) return 0;
    int n = 0;
    fields[n++] = body;
    for (char *p = body; *p && n < max_fields; p++) {
        if (*p == ',') { *p = '\0'; fields[n++] = p + 1; }
    }
    return n;
}

size_t uart_proto_hex_encode(const uint8_t *bytes, size_t n, char *out, size_t cap) {
    if (!out || cap < 2 * n + 1) return 0;
    for (size_t i = 0; i < n; i++) {
        out[2 * i]     = hex_nibble((bytes[i] >> 4) & 0x0F);
        out[2 * i + 1] = hex_nibble(bytes[i] & 0x0F);
    }
    out[2 * n] = '\0';
    return 2 * n;
}

bool uart_proto_format_mac(const uint8_t mac[6], char *out, size_t cap) {
    if (!mac || !out || cap < 18) return false;
    snprintf(out, cap, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

bool uart_proto_poll_line(char *out, size_t cap, size_t *out_len) {
    uint8_t b;
    while (uart_read_bytes(UART_PORT, &b, 1, 0) == 1) {
        if (b == '$') {
            g_in_frame = true; g_rx_pos = 0; g_rx_buf[g_rx_pos++] = '$';
            continue;
        }
        if (!g_in_frame) continue;
        if (b == '\n') {
            if (g_rx_pos < MAX_LINE_LEN) {
                if (g_rx_pos > 0 && g_rx_buf[g_rx_pos - 1] == '\r') g_rx_pos--;
                g_rx_buf[g_rx_pos] = '\0';
                size_t len = g_rx_pos;
                g_rx_pos = 0; g_in_frame = false;
                if (len < cap) { memcpy(out, g_rx_buf, len + 1); if (out_len) *out_len = len; return true; }
            }
            g_rx_pos = 0; g_in_frame = false;
            continue;
        }
        if (g_rx_pos >= MAX_LINE_LEN) { g_rx_pos = 0; g_in_frame = false; continue; }
        g_rx_buf[g_rx_pos++] = (char)b;
    }
    return false;
}
