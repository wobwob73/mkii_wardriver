#pragma once

#include "branch_defs.h"

void detection_q_init(void);
bool detection_q_push(const detection_t *e);
bool detection_q_pop(detection_t *out);
uint32_t detection_q_overflow_count(void);
uint16_t detection_q_used(void);

void wids_q_init(void);
bool wids_q_push(const wids_event_t *e);
bool wids_q_pop(wids_event_t *out);
uint32_t wids_q_overflow_count(void);
uint16_t wids_q_used(void);
