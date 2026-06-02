#include "icm42688.h"
#include "i2c_bus.h"

#include "pico/stdlib.h"

#define REG_DEVICE_CONFIG  0x11
#define REG_INT_CONFIG     0x14
#define REG_TEMP_DATA1     0x1D
#define REG_PWR_MGMT0      0x4E
#define REG_GYRO_CONFIG0   0x4F
#define REG_ACCEL_CONFIG0  0x50
#define REG_INT_SOURCE0    0x65
#define REG_WHO_AM_I       0x75
#define WHO_AM_I_VAL       0x47

#define ACCEL_LSB_PER_G    4096.0f
#define G_TO_MS2           9.80665f
#define GYRO_LSB_PER_DPS   16.4f
#define DPS_TO_RAD         (3.14159265358979f / 180.0f)

bool icm42688_init(void) {
    uint8_t who = 0;
    if (i2c_bus_read_regs(I2C_ADDR_ICM42688, REG_WHO_AM_I, &who, 1) < 0) return false;
    if (who != WHO_AM_I_VAL) return false;

    if (i2c_bus_write_reg(I2C_ADDR_ICM42688, REG_DEVICE_CONFIG, 0x01) < 0) return false;
    sleep_ms(1);

    if (i2c_bus_write_reg(I2C_ADDR_ICM42688, REG_PWR_MGMT0, 0x0F) < 0) return false;
    if (i2c_bus_write_reg(I2C_ADDR_ICM42688, REG_ACCEL_CONFIG0, 0x06) < 0) return false;
    if (i2c_bus_write_reg(I2C_ADDR_ICM42688, REG_GYRO_CONFIG0, 0x06) < 0) return false;
    if (i2c_bus_write_reg(I2C_ADDR_ICM42688, REG_INT_CONFIG, 0x18) < 0) return false;
    if (i2c_bus_write_reg(I2C_ADDR_ICM42688, REG_INT_SOURCE0, 0x08) < 0) return false;

    sleep_ms(50);
    return true;
}

bool icm42688_read(float *ax, float *ay, float *az,
                   float *gx, float *gy, float *gz) {
    uint8_t buf[14];
    if (i2c_bus_read_regs(I2C_ADDR_ICM42688, REG_TEMP_DATA1, buf, 14) < 0) return false;

    int16_t ax_raw = (int16_t)((buf[2] << 8) | buf[3]);
    int16_t ay_raw = (int16_t)((buf[4] << 8) | buf[5]);
    int16_t az_raw = (int16_t)((buf[6] << 8) | buf[7]);
    int16_t gx_raw = (int16_t)((buf[8] << 8) | buf[9]);
    int16_t gy_raw = (int16_t)((buf[10] << 8) | buf[11]);
    int16_t gz_raw = (int16_t)((buf[12] << 8) | buf[13]);

    if (ax) *ax = (float)ax_raw / ACCEL_LSB_PER_G * G_TO_MS2;
    if (ay) *ay = (float)ay_raw / ACCEL_LSB_PER_G * G_TO_MS2;
    if (az) *az = (float)az_raw / ACCEL_LSB_PER_G * G_TO_MS2;
    if (gx) *gx = (float)gx_raw / GYRO_LSB_PER_DPS * DPS_TO_RAD;
    if (gy) *gy = (float)gy_raw / GYRO_LSB_PER_DPS * DPS_TO_RAD;
    if (gz) *gz = (float)gz_raw / GYRO_LSB_PER_DPS * DPS_TO_RAD;
    return true;
}
