#pragma once

#include "leaf_defs.h"

namespace hb {

void init();

void note_scan_complete();

void note_error();

void note_command_processed();

bool due();

uint32_t uptime_s();

uint32_t free_heap();

uint32_t scan_count();

uint32_t err_count();

bool send();

bool send_immediate();

void watchdog_heap();

}
