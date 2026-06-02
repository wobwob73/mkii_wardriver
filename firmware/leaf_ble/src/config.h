#pragma once

#include "ble_defs.h"

void cfg_init(void);

/* Adopt identity + params from a $CF line. Returns false on invalid id/mode. */
bool cfg_adopt(const char *leaf_id, int mode, int phy_mask,
               int window_ms, int interval_ms);

void cfg_apply_defaults(void);

bool cfg_adopted(void);

const char *cfg_id_str(void);

uint8_t cfg_mode(void);

uint8_t cfg_phy_mask(void);

uint16_t cfg_window_ms(void);

uint16_t cfg_interval_ms(void);

/* "BLE-<digit>" convention (blebt §4.1). */
bool valid_ble_id(const char *s);
