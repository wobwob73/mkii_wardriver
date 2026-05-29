#include "core1_output.h"
#include "queues.h"
#include "proto.h"
#include "pps_time.h"
#include "stm32_uart.h"
#include "bmp390.h"
#include "scd41.h"
#include "sgp41.h"
#include "i2c_bus.h"

#include "pico/stdlib.h"
#include "pico/time.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    float baro_hpa;
    float baro_alt_m;
    float baro_temp_c;
    uint64_t baro_last_ms;

    int   co2_ppm;
    float scd_temp_c;
    float scd_humid_pct;
    uint64_t scd_last_ms;

    int  voc_index;
    int  nox_index;
    uint64_t sgp_last_ms;
    uint64_t sgp_condition_start_ms;
    bool sgp_conditioned;
} slow_cache_t;

static slow_cache_t g_cache;
static bool         g_imu_ok, g_mag_ok, g_baro_ok, g_scd_ok, g_sgp_ok;
static uint32_t     g_fusion_rate_hz = 0;
static uint32_t     g_err_count = 0;
static uint32_t     g_uptime_s = 0;
static line_receiver_t g_stm_rx;

void core1_output_init(bool imu_ok, bool mag_ok, bool baro_ok, bool scd_ok, bool sgp_ok) {
    memset(&g_cache, 0, sizeof(g_cache));
    g_cache.co2_ppm = -1;
    g_cache.voc_index = -1;
    g_cache.nox_index = -1;
    g_cache.sgp_conditioned = false;
    g_cache.sgp_condition_start_ms = to_ms_since_boot(get_absolute_time());

    g_imu_ok = imu_ok;
    g_mag_ok = mag_ok;
    g_baro_ok = baro_ok;
    g_scd_ok = scd_ok;
    g_sgp_ok = sgp_ok;
    proto_lr_init(&g_stm_rx);
}

static void send_line(const char *body) {
    char line[MAX_LINE_LEN + 1];
    size_t n = strlen(body);
    if (n + 4 >= sizeof(line)) return;
    memcpy(line, body, n + 1);
    if (!proto_finalize_line(line, sizeof(line))) return;
    stm32_uart_send_line(line, strlen(line));
}

static void emit_en(uint32_t epoch_s, uint32_t frac_us, env_sample_t *s, bool time_valid) {
    char co2_str[16], temp_str[16], humid_str[16], voc_str[16], nox_str[16];
    if (g_scd_ok && (to_ms_since_boot(get_absolute_time()) - g_cache.scd_last_ms) < SCD41_VALIDITY_MS) {
        snprintf(co2_str, sizeof(co2_str), "%d", g_cache.co2_ppm);
        snprintf(temp_str, sizeof(temp_str), "%.2f", (double)g_cache.scd_temp_c);
        snprintf(humid_str, sizeof(humid_str), "%.2f", (double)g_cache.scd_humid_pct);
    } else {
        snprintf(co2_str, sizeof(co2_str), "-1");
        snprintf(temp_str, sizeof(temp_str), "NaN");
        snprintf(humid_str, sizeof(humid_str), "NaN");
    }
    if (g_sgp_ok && g_cache.sgp_conditioned) {
        snprintf(voc_str, sizeof(voc_str), "%d", g_cache.voc_index);
        snprintf(nox_str, sizeof(nox_str), "%d", g_cache.nox_index);
    } else {
        snprintf(voc_str, sizeof(voc_str), "-1");
        snprintf(nox_str, sizeof(nox_str), "-1");
    }

    char body[MAX_LINE_LEN];
    snprintf(body, sizeof(body),
             "$EN,SEN,%lu.%06lu,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f,%.2f,%s,%s,%s,%s,%s,%u",
             (unsigned long)epoch_s, (unsigned long)frac_us,
             (double)s->heading_deg, (double)s->pitch_deg, (double)s->roll_deg,
             (double)s->ax, (double)s->ay, (double)s->az,
             (double)s->mx, (double)s->my, (double)s->mz,
             (double)g_cache.baro_hpa, (double)g_cache.baro_alt_m,
             temp_str, humid_str, co2_str, voc_str, nox_str,
             (unsigned)(time_valid ? 0 : 1));
    send_line(body);
}

static void emit_sb(void) {
    char body[160];
    snprintf(body, sizeof(body),
             "$SB,SEN,%lu,%u,%u,%u,%u,%u,%u,%u,%lu",
             (unsigned long)g_uptime_s,
             (unsigned)(pps_time_is_valid() ? 1 : 0),
             (unsigned)(g_imu_ok ? 1 : 0),
             (unsigned)(g_mag_ok ? 1 : 0),
             (unsigned)(g_baro_ok ? 1 : 0),
             (unsigned)(g_scd_ok ? 1 : 0),
             (unsigned)(g_sgp_ok && g_cache.sgp_conditioned ? 1 : 0),
             (unsigned)g_fusion_rate_hz,
             (unsigned long)(g_err_count + env_q_overflow_count() + i2c_bus_errors()));
    send_line(body);
}

static void poll_baro(uint64_t now_ms) {
    if (!g_baro_ok) return;
    if ((now_ms - g_cache.baro_last_ms) < BARO_PERIOD_MS) return;
    if (bmp390_read(&g_cache.baro_hpa, &g_cache.baro_alt_m, &g_cache.baro_temp_c)) {
        g_cache.baro_last_ms = now_ms;
    } else {
        g_err_count++;
    }
}

static void poll_scd(uint64_t now_ms) {
    if (!g_scd_ok) return;
    if ((now_ms - g_cache.scd_last_ms) < SCD41_PERIOD_MS) return;
    bool ready = false;
    if (!scd41_data_ready(&ready)) { g_err_count++; return; }
    if (!ready) return;
    if (!scd41_read(&g_cache.co2_ppm, &g_cache.scd_temp_c, &g_cache.scd_humid_pct)) {
        g_err_count++;
        return;
    }
    g_cache.scd_last_ms = now_ms;
}

static void poll_sgp(uint64_t now_ms) {
    if (!g_sgp_ok) return;
    if (!g_cache.sgp_conditioned) {
        if ((now_ms - g_cache.sgp_condition_start_ms) >= SGP41_CONDITIONING_MS) {
            sgp41_finish_conditioning();
            g_cache.sgp_conditioned = true;
        }
        return;
    }
    if ((now_ms - g_cache.sgp_last_ms) < SGP41_PERIOD_MS) return;
    uint16_t raw_voc = 0, raw_nox = 0;
    float rh = (g_scd_ok && (now_ms - g_cache.scd_last_ms) < SCD41_VALIDITY_MS)
                   ? g_cache.scd_humid_pct : 50.0f;
    float t = (g_scd_ok && (now_ms - g_cache.scd_last_ms) < SCD41_VALIDITY_MS)
                   ? g_cache.scd_temp_c : 25.0f;
    if (!sgp41_measure_raw(rh, t, &raw_voc, &raw_nox)) { g_err_count++; return; }
    sgp41_run_gas_index(raw_voc, raw_nox, &g_cache.voc_index, &g_cache.nox_index);
    g_cache.sgp_last_ms = now_ms;
}

static void process_stm_line(char *line, size_t len) {
    if (!proto_validate_line(line, len)) { g_err_count++; return; }
    char *star = NULL;
    for (size_t i = 1; i < len; i++) {
        if (line[i] == '*') { star = line + i; break; }
    }
    if (!star) return;
    *star = '\0';
    char *fields[16];
    int n = proto_split_body(line + 1, fields, 16);
    if (n < 1) return;
    if (strcmp(fields[0], "TM") == 0 && n >= 3) {
        uint32_t epoch = (uint32_t)atol(fields[1]);
        bool fix_ok = atoi(fields[2]) != 0;
        pps_time_apply_tm(epoch, fix_ok);
    } else if (strcmp(fields[0], "RQ") == 0) {
        emit_sb();
    }
}

void core1_output_run(void) {
    uint64_t en_next_us = time_us_64() + EN_OUTPUT_PERIOD_US;
    uint32_t sb_last_ms = to_ms_since_boot(get_absolute_time());
    uint32_t boot_ms = sb_last_ms;
    uint64_t fusion_window_ms = boot_ms;
    uint32_t fusion_window_samples = 0;

    while (1) {
        uint64_t now_us = time_us_64();
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        g_uptime_s = (now_ms - boot_ms) / 1000;

        uint8_t rxbuf[64];
        int rn = stm32_uart_read(rxbuf, sizeof(rxbuf));
        for (int i = 0; i < rn; i++) {
            proto_lr_feed(&g_stm_rx, rxbuf[i]);
            if (proto_lr_has_line(&g_stm_rx)) {
                char copy[MAX_LINE_LEN + 1];
                size_t llen = g_stm_rx.len;
                if (llen > MAX_LINE_LEN) llen = MAX_LINE_LEN;
                memcpy(copy, g_stm_rx.buf, llen);
                copy[llen] = '\0';
                proto_lr_clear(&g_stm_rx);
                process_stm_line(copy, llen);
            }
        }

        pps_time_health_check();

        poll_baro(now_ms);
        poll_scd(now_ms);
        poll_sgp(now_ms);

        env_sample_t s;
        env_sample_t latest = {0};
        bool have = false;
        while (env_q_pop(&s)) {
            latest = s;
            have = true;
            fusion_window_samples++;
        }

        if (now_us >= en_next_us) {
            uint32_t epoch_s = 0, frac_us = 0;
            bool tv = pps_time_compute(latest.local_timer_us, &epoch_s, &frac_us);
            if (have) {
                emit_en(epoch_s, frac_us, &latest, tv);
            }
            en_next_us += EN_OUTPUT_PERIOD_US;
        }

        if ((now_ms - sb_last_ms) >= SB_OUTPUT_PERIOD_MS) {
            uint32_t dt_ms = (uint32_t)(now_ms - fusion_window_ms);
            if (dt_ms > 0) {
                g_fusion_rate_hz = (fusion_window_samples * 1000U) / dt_ms;
            }
            fusion_window_ms = now_ms;
            fusion_window_samples = 0;
            emit_sb();
            sb_last_ms = now_ms;
        }
    }
}
