#pragma once

#include "env_defs.h"

void env_q_init(void);
bool env_q_push(const env_sample_t *s);
bool env_q_pop(env_sample_t *out);
uint32_t env_q_overflow_count(void);
uint16_t env_q_used(void);
