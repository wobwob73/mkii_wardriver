#include "sgp41.h"
#include "i2c_bus.h"

#include "pico/stdlib.h"

#include <string.h>

/*
 * SGP41 driver — minimal listen-only path.
 *
 * The Sensirion Gas Index Algorithm proper is ~1000 lines of BSD-
 * licensed reference C from Sensirion. For v1.0 we ship a placeholder
 * that returns:
 *   voc_index = 100 (algorithm baseline) when raw_voc is in plausible range
 *   nox_index = 1   (algorithm baseline)
 * and lets the firmware compile + flash without the vendor library.
 *
 * To upgrade to the real algorithm:
 *   - drop sensirion_gas_index_algorithm.{c,h} from
 *     https://github.com/Sensirion/gas-index-algorithm into
 *     third_party/sensirion_gas_index_algorithm/
 *   - add them to CMakeLists.txt
 *   - replace sgp41_run_gas_index() body with calls to
 *     GasIndexAlgorithm_process(...).
 */

#define CMD_SELF_TEST              0x280E
#define CMD_EXEC_CONDITIONING      0x2612
#define CMD_MEASURE_RAW_SIGNALS    0x2619
#define CMD_GET_SERIAL_NUMBER      0x3682

static bool g_conditioned = false;

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

static int send_cmd_with_args(uint16_t cmd, const uint16_t *args, size_t n_args) {
    uint8_t buf[2 + 6];
    if (2 + n_args * 3 > sizeof(buf)) return -1;
    buf[0] = (uint8_t)(cmd >> 8);
    buf[1] = (uint8_t)(cmd & 0xFF);
    for (size_t i = 0; i < n_args; i++) {
        buf[2 + i * 3]     = (uint8_t)(args[i] >> 8);
        buf[2 + i * 3 + 1] = (uint8_t)(args[i] & 0xFF);
        buf[2 + i * 3 + 2] = crc8(&buf[2 + i * 3], 2);
    }
    return i2c_bus_write(I2C_ADDR_SGP41, buf, 2 + n_args * 3);
}

static int read_words(uint16_t cmd, uint16_t *out_words, size_t n_words, uint32_t delay_ms) {
    if (send_cmd_with_args(cmd, NULL, 0) < 2) return -1;
    if (delay_ms) sleep_ms(delay_ms);
    size_t total = n_words * 3;
    uint8_t buf[12];
    if (total > sizeof(buf)) return -1;
    if (i2c_bus_read(I2C_ADDR_SGP41, buf, total) != (int)total) return -1;
    for (size_t i = 0; i < n_words; i++) {
        uint8_t *p = &buf[i * 3];
        if (crc8(p, 2) != p[2]) return -1;
        out_words[i] = (uint16_t)((p[0] << 8) | p[1]);
    }
    return 0;
}

bool sgp41_init(void) {
    g_conditioned = false;
    uint16_t serial[3];
    if (read_words(CMD_GET_SERIAL_NUMBER, serial, 3, 1) != 0) return false;
    return true;
}

bool sgp41_start_conditioning(void) {
    uint16_t args[2] = { 0x8000, 0x6666 };
    if (send_cmd_with_args(CMD_EXEC_CONDITIONING, args, 2) < 8) return false;
    return true;
}

bool sgp41_finish_conditioning(void) {
    g_conditioned = true;
    return true;
}

bool sgp41_measure_raw(float humid_pct, float temp_c, uint16_t *raw_voc, uint16_t *raw_nox) {
    float rh = humid_pct;
    float t = temp_c;
    if (rh < 0) rh = 50.0f;
    if (rh > 100) rh = 100.0f;
    if (t < -40) t = 25.0f;
    if (t > 85) t = 25.0f;

    uint16_t rh_ticks = (uint16_t)((rh / 100.0f) * 65535.0f);
    uint16_t t_ticks = (uint16_t)(((t + 45.0f) / 175.0f) * 65535.0f);
    uint16_t args[2] = { rh_ticks, t_ticks };
    if (send_cmd_with_args(CMD_MEASURE_RAW_SIGNALS, args, 2) < 8) return false;
    sleep_ms(50);
    uint8_t buf[6];
    if (i2c_bus_read(I2C_ADDR_SGP41, buf, 6) != 6) return false;
    if (crc8(&buf[0], 2) != buf[2]) return false;
    if (crc8(&buf[3], 2) != buf[5]) return false;
    if (raw_voc) *raw_voc = (uint16_t)((buf[0] << 8) | buf[1]);
    if (raw_nox) *raw_nox = (uint16_t)((buf[3] << 8) | buf[4]);
    return true;
}

void sgp41_run_gas_index(uint16_t raw_voc, uint16_t raw_nox, int *voc_index, int *nox_index) {
    if (!g_conditioned) {
        if (voc_index) *voc_index = -1;
        if (nox_index) *nox_index = -1;
        return;
    }
    (void)raw_voc;
    (void)raw_nox;
    if (voc_index) *voc_index = 100;
    if (nox_index) *nox_index = 1;
}
