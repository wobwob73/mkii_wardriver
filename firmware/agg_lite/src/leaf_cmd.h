#pragma once

#include "agg_defs.h"

/* Downstream command construction ($CF/$CH/$PG/$RB), one TX SM retargeted per
 * slot. $CF content is per-family (§6.4):
 *   W24 : $CF,WH1,2,0,200,0       scan-hop ch 1/6/11 (v1.3 amendment)
 *   W5G : $CF,W5_1,0,3,200,0      5 GHz scan, UNII-1+2A
 *   BLE : $CF,BLE-1,0,3,1000,1000 BLE both PHYs, 100% duty
 */

void leaf_cmd_send_cf(uint8_t slot);

void leaf_cmd_send_ch(uint8_t slot, uint16_t mask);

void leaf_cmd_send_pg(uint8_t slot);

void leaf_cmd_send_rb(uint8_t slot);

unsigned int tx_pin_for(uint8_t slot);
