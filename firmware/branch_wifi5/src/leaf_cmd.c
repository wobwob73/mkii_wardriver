#include "leaf_cmd.h"
#include "proto.h"
#include "pio_uart.h"

#include <string.h>
#include <stdio.h>

unsigned int tx_pin_for(uint8_t leaf_idx) {
    switch (leaf_idx) {
        case LEAF_W5_1: return LEAF_W5_1_TX_PIN;
        case LEAF_W5_2: return LEAF_W5_2_TX_PIN;
        case LEAF_W5_3: return LEAF_W5_3_TX_PIN;
        default:        return LEAF_W5_1_TX_PIN;
    }
}

// $CF,leaf_id,mode,channel_set_id,dwell_ms,0 (5-field body after type)
void leaf_cmd_send_cf(uint8_t leaf_idx) {
    char line[64];
    snprintf(line, sizeof(line), "$CF,%s,%u,%u,%u,0",
             leaf_id_str(leaf_idx),
             (unsigned)leaf_default_mode(leaf_idx),
             (unsigned)leaf_default_channel_set(leaf_idx),
             (unsigned)leaf_default_dwell(leaf_idx));
    if (!proto_finalize_line(line, sizeof(line))) return;
    pio_uart_tx_send_line(tx_pin_for(leaf_idx), line);
}

// $CH,leaf_id,bitmask_lo,bitmask_hi
void leaf_cmd_send_ch(uint8_t leaf_idx, uint32_t mask) {
    uint16_t lo = (uint16_t)(mask & 0xFFFFu);
    uint16_t hi = (uint16_t)((mask >> 16) & 0xFFFFu);
    char line[32];
    snprintf(line, sizeof(line), "$CH,%s,%u,%u",
             leaf_id_str(leaf_idx), (unsigned)lo, (unsigned)hi);
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
