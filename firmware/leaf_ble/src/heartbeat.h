#pragma once

#include "ble_defs.h"

void hb_init(void);

void hb_note_scan_window(void);   /* one completed 1 s scan window */

void hb_note_error(void);

bool hb_due(void);

bool hb_send(void);

bool hb_send_immediate(void);

uint32_t hb_scan_count(void);
