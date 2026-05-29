#include "scd41.h"
#include "i2c_bus.h"

#include "pico/stdlib.h"

#define CMD_STOP_PERIODIC      0x3F86
#define CMD_START_PERIODIC     0x21B1
#define CMD_GET_DATA_READY     0xE4B8
#define CMD_READ_MEASUREMENT   0xEC05
#define CMD_GET_SERIAL         0x3682

static uint8_t crc8(const uint8_t *data, size_t n) {
    uint8_t crc = 0xFF;
    for (size_t b = 0; b < n; b++) {
        crc ^= data[b];
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
        }
    }
    return crc;
}

static int send_cmd(uint16_t cmd) {
    uint8_t buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };
    return i2c_bus_write(I2C_ADDR_SCD41, buf, 2);
}

static int read_words(uint16_t cmd, uint16_t *out_words, size_t n_words, uint32_t delay_ms) {
    if (send_cmd(cmd) < 2) return -1;
    if (delay_ms) sleep_ms(delay_ms);
    size_t total = n_words * 3;
    uint8_t buf[18];
    if (total > sizeof(buf)) return -1;
    if (i2c_bus_read(I2C_ADDR_SCD41, buf, total) != (int)total) return -1;
    for (size_t i = 0; i < n_words; i++) {
        uint8_t *p = &buf[i * 3];
        if (crc8(p, 2) != p[2]) return -1;
        out_words[i] = (uint16_t)((p[0] << 8) | p[1]);
    }
    return 0;
}

bool scd41_init(void) {
    send_cmd(CMD_STOP_PERIODIC);
    sleep_ms(500);
    uint16_t serial[3];
    if (read_words(CMD_GET_SERIAL, serial, 3, 1) != 0) return false;
    if (send_cmd(CMD_START_PERIODIC) < 2) return false;
    return true;
}

bool scd41_data_ready(bool *ready) {
    uint16_t status;
    if (read_words(CMD_GET_DATA_READY, &status, 1, 1) != 0) return false;
    if (ready) *ready = (status & 0x07FF) != 0;
    return true;
}

bool scd41_read(int *co2_ppm, float *temp_c, float *humid_pct) {
    uint16_t w[3];
    if (read_words(CMD_READ_MEASUREMENT, w, 3, 1) != 0) return false;
    if (co2_ppm) *co2_ppm = (int)w[0];
    if (temp_c) *temp_c = -45.0f + 175.0f * (float)w[1] / 65535.0f;
    if (humid_pct) *humid_pct = 100.0f * (float)w[2] / 65535.0f;
    return true;
}
