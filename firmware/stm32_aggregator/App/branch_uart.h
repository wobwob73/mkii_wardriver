#pragma once

#include "stm32_config.h"
#include "proto.h"

typedef void (*branch_line_cb_t)(uint8_t branch_idx, const char *line, size_t len, void *ctx);

void branch_uart_init(void);

void branch_uart_register_cb(branch_line_cb_t cb, void *ctx);

void branch_uart_pump(void);

uint32_t branch_uart_lines_ok(uint8_t branch_idx);

uint32_t branch_uart_lines_bad(uint8_t branch_idx);
