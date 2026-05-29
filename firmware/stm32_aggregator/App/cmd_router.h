#pragma once

#include "stm32_config.h"

int cmd_router_target_for_leaf(const char *leaf_id);

bool cmd_router_handle_usb_line(const char *line, size_t len);
