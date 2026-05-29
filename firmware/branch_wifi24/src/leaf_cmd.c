#include "leaf_cmd.h"
#include "proto.h"
#include "pio_uart.h"

#include <string.h>
#include <stdio.h>

uint tx_pin_for(uint8_t leaf_idx) {
    switch (leaf_idx) {
        case LEAF_W1: return LEAF_W1_TX_PIN;
        case LEAF_W2: return LEAF_W2_TX_PIN;
        case LEAF_W3: return LEAF_W3_TX_PIN;
        case LEAF_W4: return LEAF_W4_TX_PIN;
        default:      return LEAF_W1_TX_PIN;
    }
}

void leaf_cmd_send_cf(uint8_t leaf_idx) {
    char line[64];
    snprintf(line, sizeof(line), "$CF,%s,%u,%u,%u,0",
             leaf_id_str(leaf_idx),
             (unsigned)leaf_default_mode(leaf_idx),
             (unsigned)leaf_default_channel(leaf_idx),
             (unsigned)leaf_default_dwell(leaf_idx));
    if (!proto_finalize_line(line, sizeof(line))) return;
    pio_uart_tx_send_line(tx_pin_for(leaf_idx), line);
}

void leaf_cmd_send_ch(uint8_t leaf_idx, uint16_t mask) {
    char line[32];
    snprintf(line, sizeof(line), "$CH,%s,%u",
             leaf_id_str(leaf_idx), (unsigned)mask);
    if (!proto_finalize_line(line, sizeof(line))) return;
    pio_uart_tx_send_line(tx_pin_for(leaf_idx), line);
}

void leaf_cmd_send_pg(uint8_t leaf_idx) {
    char line[16];
    snprintf(line, sizeof(line), "$PG,%s", leaf_id_str(leaf_idx));
    if (!proto_finalize_line(line, sizeof(line))) return;
    pio_uart_tx_send_line(tx_pin_for(leaf_idx), line);
}

void leaf_cmd_send_rb(uint8_t leaf_idx) {
    char line[16];
    snprintf(line, sizeof(line), "$RB,%s", leaf_id_str(leaf_idx));
    if (!proto_finalize_line(line, sizeof(line))) return;
    pio_uart_tx_send_line(tx_pin_for(leaf_idx), line);
}

void leaf_cmd_relay(uint8_t leaf_idx, const char *inner_line) {
    if (!inner_line) return;
    if (leaf_idx >= N_LEAVES) return;
    pio_uart_tx_send_line(tx_pin_for(leaf_idx), inner_line);
}
