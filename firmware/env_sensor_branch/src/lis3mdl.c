#include "lis3mdl.h"
#include "i2c_bus.h"

#define REG_WHO_AM_I  0x0F
#define REG_CTRL_REG1 0x20
#define REG_CTRL_REG2 0x21
#define REG_CTRL_REG3 0x22
#define REG_CTRL_REG4 0x23
#define REG_OUT_X_L   0x28
#define WHO_AM_I_VAL  0x3D

#define LSB_PER_GAUSS 6842.0f
#define GAUSS_TO_UT   100.0f

bool lis3mdl_init(void) {
    uint8_t who = 0;
    if (i2c_bus_read_regs(I2C_ADDR_LIS3MDL, REG_WHO_AM_I, &who, 1) < 0) return false;
    if (who != WHO_AM_I_VAL) return false;

    if (i2c_bus_write_reg(I2C_ADDR_LIS3MDL, REG_CTRL_REG1, 0x70) < 0) return false;
    if (i2c_bus_write_reg(I2C_ADDR_LIS3MDL, REG_CTRL_REG2, 0x00) < 0) return false;
    if (i2c_bus_write_reg(I2C_ADDR_LIS3MDL, REG_CTRL_REG3, 0x00) < 0) return false;
    if (i2c_bus_write_reg(I2C_ADDR_LIS3MDL, REG_CTRL_REG4, 0x0C) < 0) return false;
    return true;
}

bool lis3mdl_read(float *mx_uT, float *my_uT, float *mz_uT) {
    uint8_t buf[6];
    uint8_t reg = REG_OUT_X_L | 0x80;
    if (i2c_bus_read_regs(I2C_ADDR_LIS3MDL, reg, buf, 6) < 0) return false;
    int16_t x = (int16_t)((buf[1] << 8) | buf[0]);
    int16_t y = (int16_t)((buf[3] << 8) | buf[2]);
    int16_t z = (int16_t)((buf[5] << 8) | buf[4]);
    if (mx_uT) *mx_uT = (float)x / LSB_PER_GAUSS * GAUSS_TO_UT;
    if (my_uT) *my_uT = (float)y / LSB_PER_GAUSS * GAUSS_TO_UT;
    if (mz_uT) *mz_uT = (float)z / LSB_PER_GAUSS * GAUSS_TO_UT;
    return true;
}
