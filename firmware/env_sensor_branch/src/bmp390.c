#include "bmp390.h"
#include "i2c_bus.h"

#include "pico/stdlib.h"

#include <math.h>

#define REG_CHIP_ID  0x00
#define REG_DATA_0   0x04
#define REG_PWR_CTRL 0x1B
#define REG_OSR      0x1C
#define REG_ODR      0x1D
#define REG_CONFIG   0x1F
#define REG_CMD      0x7E
#define REG_CALIB    0x31
#define CHIP_ID_VAL  0x60

#define P0_PA        101325.0f

typedef struct {
    float par_t1, par_t2, par_t3;
    float par_p1, par_p2, par_p3, par_p4, par_p5;
    float par_p6, par_p7, par_p8, par_p9, par_p10, par_p11;
    float t_lin;
} bmp390_calib_t;

static bmp390_calib_t g_cal;

static void load_calib(void) {
    uint8_t raw[21];
    if (i2c_bus_read_regs(I2C_ADDR_BMP390, REG_CALIB, raw, 21) < 0) return;

    uint16_t t1 = (uint16_t)((raw[1] << 8) | raw[0]);
    uint16_t t2 = (uint16_t)((raw[3] << 8) | raw[2]);
    int8_t   t3 = (int8_t)raw[4];
    int16_t  p1 = (int16_t)((raw[6] << 8) | raw[5]);
    int16_t  p2 = (int16_t)((raw[8] << 8) | raw[7]);
    int8_t   p3 = (int8_t)raw[9];
    int8_t   p4 = (int8_t)raw[10];
    uint16_t p5 = (uint16_t)((raw[12] << 8) | raw[11]);
    uint16_t p6 = (uint16_t)((raw[14] << 8) | raw[13]);
    int8_t   p7 = (int8_t)raw[15];
    int8_t   p8 = (int8_t)raw[16];
    int16_t  p9 = (int16_t)((raw[18] << 8) | raw[17]);
    int8_t   p10 = (int8_t)raw[19];
    int8_t   p11 = (int8_t)raw[20];

    g_cal.par_t1 = (float)t1 * 256.0f;
    g_cal.par_t2 = (float)t2 / 1073741824.0f;
    g_cal.par_t3 = (float)t3 / 281474976710656.0f;
    g_cal.par_p1 = ((float)p1 - 16384.0f) / 1048576.0f;
    g_cal.par_p2 = ((float)p2 - 16384.0f) / 536870912.0f;
    g_cal.par_p3 = (float)p3 / 4294967296.0f;
    g_cal.par_p4 = (float)p4 / 137438953472.0f;
    g_cal.par_p5 = (float)p5 * 8.0f;
    g_cal.par_p6 = (float)p6 / 64.0f;
    g_cal.par_p7 = (float)p7 / 256.0f;
    g_cal.par_p8 = (float)p8 / 32768.0f;
    g_cal.par_p9 = (float)p9 / 281474976710656.0f;
    g_cal.par_p10 = (float)p10 / 281474976710656.0f;
    g_cal.par_p11 = (float)p11 / 36893488147419103232.0f;
}

bool bmp390_init(void) {
    uint8_t id = 0;
    if (i2c_bus_read_regs(I2C_ADDR_BMP390, REG_CHIP_ID, &id, 1) < 0) return false;
    if (id != CHIP_ID_VAL) return false;

    if (i2c_bus_write_reg(I2C_ADDR_BMP390, REG_CMD, 0xB6) < 0) return false;
    sleep_ms(5);

    if (i2c_bus_write_reg(I2C_ADDR_BMP390, REG_PWR_CTRL, 0x33) < 0) return false;
    if (i2c_bus_write_reg(I2C_ADDR_BMP390, REG_OSR, 0x03) < 0) return false;
    if (i2c_bus_write_reg(I2C_ADDR_BMP390, REG_ODR, 0x02) < 0) return false;
    if (i2c_bus_write_reg(I2C_ADDR_BMP390, REG_CONFIG, 0x02) < 0) return false;

    load_calib();
    sleep_ms(50);
    return true;
}

static float compensate_t(uint32_t raw_t) {
    float pd1 = (float)raw_t - g_cal.par_t1;
    float pd2 = pd1 * g_cal.par_t2;
    float t = pd2 + (pd1 * pd1) * g_cal.par_t3;
    g_cal.t_lin = t;
    return t;
}

static float compensate_p(uint32_t raw_p) {
    float t = g_cal.t_lin;
    float pd1 = g_cal.par_p6 * t;
    float pd2 = g_cal.par_p7 * (t * t);
    float pd3 = g_cal.par_p8 * (t * t * t);
    float po1 = g_cal.par_p5 + pd1 + pd2 + pd3;

    pd1 = g_cal.par_p2 * t;
    pd2 = g_cal.par_p3 * (t * t);
    pd3 = g_cal.par_p4 * (t * t * t);
    float po2 = (float)raw_p * (g_cal.par_p1 + pd1 + pd2 + pd3);

    pd1 = (float)raw_p * (float)raw_p;
    pd2 = g_cal.par_p9 + g_cal.par_p10 * t;
    pd3 = pd1 * pd2;
    float po3 = pd3 + ((float)raw_p * (float)raw_p * (float)raw_p) * g_cal.par_p11;

    return po1 + po2 + po3;
}

bool bmp390_read(float *press_hpa, float *alt_m, float *temp_c) {
    uint8_t buf[6];
    if (i2c_bus_read_regs(I2C_ADDR_BMP390, REG_DATA_0, buf, 6) < 0) return false;
    uint32_t raw_p = ((uint32_t)buf[2] << 16) | ((uint32_t)buf[1] << 8) | buf[0];
    uint32_t raw_t = ((uint32_t)buf[5] << 16) | ((uint32_t)buf[4] << 8) | buf[3];

    float t = compensate_t(raw_t);
    float p_pa = compensate_p(raw_p);

    if (temp_c) *temp_c = t;
    if (press_hpa) *press_hpa = p_pa / 100.0f;
    if (alt_m) {
        float ratio = p_pa / P0_PA;
        *alt_m = 44330.0f * (1.0f - powf(ratio, 1.0f / 5.255f));
    }
    return true;
}
