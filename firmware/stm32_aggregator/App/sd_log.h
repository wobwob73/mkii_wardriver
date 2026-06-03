#pragma once

#include "stm32_config.h"

void sd_log_init(void);

bool sd_log_ok(void);

void sd_log_set_session(const char *session_id);

int sd_log_write_line(const char *line, size_t len);

int sd_log_write_gps_snapshot(const char *line, size_t len);

int sd_log_write_event(const char *line, size_t len);

void sd_log_tick(void);

uint32_t sd_log_bytes_written(void);
