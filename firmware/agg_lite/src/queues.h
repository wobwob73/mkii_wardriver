#pragma once

#include "agg_defs.h"

/* Lock-free SPSC rings (Core 0 producer, Core 1 consumer), one set per family.
 * Same single-producer/single-consumer discipline as the BC queues — the only
 * change is three coexisting families instead of one. */

void queues_init(void);

/* WiFi detections, keyed by family index (SLOT_W24 / SLOT_W5G). */
bool     wifi_det_q_push(uint8_t fam, const detection_t *e);
bool     wifi_det_q_pop(uint8_t fam, detection_t *out);
uint16_t wifi_det_q_used(uint8_t fam);

/* BLE adv events ($BL). */
bool     ble_q_push(const ble_event_t *e);
bool     ble_q_pop(ble_event_t *out);
uint16_t ble_q_used(void);

/* BLE extended payload ($BX) pass-through. */
bool     ble_ext_q_push(const ble_ext_t *e);
bool     ble_ext_q_pop(ble_ext_t *out);

/* Cumulative overflow drops across all queues (folds into $LA.err_count). */
uint32_t queues_overflow_count(void);
