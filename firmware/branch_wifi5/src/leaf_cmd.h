#pragma once

#include "branch_defs.h"

void leaf_cmd_send_cf(uint8_t leaf_idx);
void leaf_cmd_send_ch(uint8_t leaf_idx, uint32_t mask);
void leaf_cmd_send_pg(uint8_t leaf_idx);
void leaf_cmd_send_rb(uint8_t leaf_idx);
void leaf_cmd_relay(uint8_t leaf_idx, const char *inner_line);

unsigned int tx_pin_for(uint8_t leaf_idx);
