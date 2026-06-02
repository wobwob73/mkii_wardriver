#include "i2c_bus.h"

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

static uint32_t g_err = 0;

void i2c_bus_init(void) {
    i2c_init(I2C_BUS_INST, I2C_BAUD);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);
}

int i2c_bus_write(uint8_t addr, const uint8_t *bytes, size_t n) {
    int r = i2c_write_blocking(I2C_BUS_INST, addr, bytes, n, false);
    if (r != (int)n) g_err++;
    return r;
}

int i2c_bus_read(uint8_t addr, uint8_t *out, size_t n) {
    int r = i2c_read_blocking(I2C_BUS_INST, addr, out, n, false);
    if (r != (int)n) g_err++;
    return r;
}

int i2c_bus_write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
    uint8_t buf[2] = { reg, val };
    return i2c_bus_write(addr, buf, 2);
}

int i2c_bus_read_regs(uint8_t addr, uint8_t reg, uint8_t *out, size_t n) {
    int r = i2c_write_blocking(I2C_BUS_INST, addr, &reg, 1, true);
    if (r != 1) { g_err++; return -1; }
    r = i2c_read_blocking(I2C_BUS_INST, addr, out, n, false);
    if (r != (int)n) g_err++;
    return r;
}

void i2c_bus_recover(void) {
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_SIO);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(I2C_SCL_PIN, true);
    for (int i = 0; i < 9; i++) {
        gpio_put(I2C_SCL_PIN, 0);
        sleep_us(5);
        gpio_put(I2C_SCL_PIN, 1);
        sleep_us(5);
    }
    i2c_bus_init();
}

uint32_t i2c_bus_errors(void) { return g_err; }
