#pragma once

#include "env_defs.h"

void i2c_bus_init(void);

int i2c_bus_write(uint8_t addr, const uint8_t *bytes, size_t n);
int i2c_bus_read(uint8_t addr, uint8_t *out, size_t n);

int i2c_bus_write_reg(uint8_t addr, uint8_t reg, uint8_t val);
int i2c_bus_read_regs(uint8_t addr, uint8_t reg, uint8_t *out, size_t n);

void i2c_bus_recover(void);

uint32_t i2c_bus_errors(void);
