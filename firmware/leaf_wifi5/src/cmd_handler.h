#pragma once

#include <stddef.h>

namespace cmd_handler {

void init();
void handle_line(char *line, size_t len);
void apply_initial_mode();

}
