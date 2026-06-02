#pragma once

#include "stm32_config.h"

void dispatch_init(void);

void dispatch_install(void);

void dispatch_branch_line(uint8_t branch_idx, const char *line, size_t len);

uint32_t dispatch_lines_forwarded(void);

uint32_t dispatch_unknown_prefix(void);
