#pragma once

#include "stm32_config.h"
#include "proto.h"

typedef void (*usb_line_cb_t)(const char *line, size_t len, void *ctx);

void usb_cdc_app_init(void);

void usb_cdc_app_register_cb(usb_line_cb_t cb, void *ctx);

void usb_cdc_app_pump(void);

void usb_cdc_app_send_line(const char *line, size_t len);

uint32_t usb_cdc_app_lines_in(void);

uint32_t usb_cdc_app_lines_out(void);
