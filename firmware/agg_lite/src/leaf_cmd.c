#include "leaf_cmd.h"
#include "proto.h"
#include "pio_uart.h"

#include <string.h>
#include <stdio.h>

unsigned int tx_pin_for(uint8_t slot) {
    switch (slot) {
        case SLOT_W24: return LEAF_W24_TX_PIN;
        case SLOT_W5G: return LEAF_W5G_TX_PIN;
        case SLOT_BLE: return LEAF_BLE_TX_PIN;
        default:       return LEAF_W24_TX_PIN;
    }
}

void leaf_cmd_send_cf(uint8_t slot) {
    char line[64];
    switch (slot) {
        case SLOT_W24:
            /* Scan-hop, channel 0 (hop set from mask, default 1057=1/6/11),
             * 200 ms/ch dwell. wifi24 Leaf protocol v1.3 §4.1. */
            snprintf(line, sizeof(line), "$CF,%s,2,0,200,0", slot_leaf_id(slot));
            break;
        case SLOT_W5G:
            /* 5 GHz scan, channel-set 3 (UNII-1 + UNII-2A), 200 ms dwell. */
            snprintf(line, sizeof(line), "$CF,%s,0,3,200,0", slot_leaf_id(slot));
            break;
        case SLOT_BLE:
            /* BLE scan, phy_mask 3 (1M + Coded), 1000 ms window / 1000 ms
             * interval = 100% duty. blebt_branch_v1_0.md §6. */
            snprintf(line, sizeof(line), "$CF,%s,0,3,1000,1000", slot_leaf_id(slot));
            break;
        default:
            return;
    }
    if (!proto_finalize_line(line, sizeof(line))) return;
    pio_uart_tx_send_line(tx_pin_for(slot), line);
}

void leaf_cmd_send_ch(uint8_t slot, uint16_t mask) {
    char line[32];
    snprintf(line, sizeof(line), "$CH,%s,%u", slot_leaf_id(slot), (unsigned)mask);
    if (!proto_finalize_line(line, sizeof(line))) return;
    pio_uart_tx_send_line(tx_pin_for(slot), line);
}

void leaf_cmd_send_pg(uint8_t slot) {
    char line[24];
    snprintf(line, sizeof(line), "$PG,%s", slot_leaf_id(slot));
    if (!proto_finalize_line(line, sizeof(line))) return;
    pio_uart_tx_send_line(tx_pin_for(slot), line);
}

void leaf_cmd_send_rb(uint8_t slot) {
    char line[24];
    snprintf(line, sizeof(line), "$RB,%s", slot_leaf_id(slot));
    if (!proto_finalize_line(line, sizeof(line))) return;
    pio_uart_tx_send_line(tx_pin_for(slot), line);
}
