#pragma once

#include "ble_defs.h"

/* NimBLE passive observer. Brings up the NimBLE host, starts an extended scan
 * across 1M + Coded primary advertising, and parses each report into a
 * BleAdvEvent on a FreeRTOS queue. The host callback NEVER touches the UART —
 * it only enqueues (blebt §7.3 critical rule). The main loop drains via
 * ble_scan_pop() and emits $BL/$BX. */

void ble_scan_init(uint8_t phy_mask);

/* Pop one parsed event (non-blocking). Returns false if none queued. */
bool ble_scan_pop(BleAdvEvent *out);

/* True once the host has synced and scanning has started. */
bool ble_scan_running(void);
