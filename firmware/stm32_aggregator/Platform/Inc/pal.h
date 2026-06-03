#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "stm32_config.h"

void pal_init(void);

uint64_t pal_time_us_64(void);

uint32_t pal_time_ms(void);

int pal_branch_rx_read(uint8_t branch_idx, uint8_t *out, size_t cap);

int pal_branch_tx_write(uint8_t branch_idx, const uint8_t *bytes, size_t len);

int pal_branch_tx_write_str(uint8_t branch_idx, const char *line);

int pal_usb_cdc_write(const uint8_t *bytes, size_t len);

int pal_usb_cdc_write_str(const char *line);

int pal_usb_cdc_read(uint8_t *out, size_t cap);

bool pal_usb_cdc_attached(void);

int pal_i2c_read_reg(uint8_t i2c_addr, uint8_t reg, uint8_t *out, size_t n);

int pal_i2c_write_reg(uint8_t i2c_addr, uint8_t reg, const uint8_t *bytes, size_t n);

int pal_i2c_read(uint8_t i2c_addr, uint8_t *out, size_t n);

int pal_i2c_write(uint8_t i2c_addr, const uint8_t *bytes, size_t n);

typedef void (*pal_pps_cb_t)(uint64_t timer_us);

void pal_pps_register(pal_pps_cb_t cb);

bool pal_sd_present(void);

int pal_sd_mount(void);

int pal_sd_open_append(const char *path, void **handle_out);

int pal_sd_write(void *handle, const uint8_t *bytes, size_t n);

int pal_sd_flush(void *handle);

int pal_sd_close(void *handle);

int pal_sd_mkdir_p(const char *path);

typedef enum {
    PAL_LED_BOOT_OK = 0,
    PAL_LED_TIME_DEGRADED = 1,
    PAL_LED_FAULT = 2,
} pal_led_t;

void pal_led_set(pal_led_t led, bool on);

void pal_led_blink_ms(pal_led_t led, uint32_t period_ms);

bool pal_button_held(void);

void pal_iwdg_kick(void);

void pal_reboot(void);
