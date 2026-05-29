#pragma once

#include "env_defs.h"

void stm32_uart_init(void);

void stm32_uart_send_line(const char *line, size_t len);

int stm32_uart_read(uint8_t *out, size_t cap);
