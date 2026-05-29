#include "app_main.h"
#include "proto.h"
#include "pps_time.h"
#include "gps.h"
#include "branch_uart.h"
#include "usb_cdc_app.h"
#include "cmd_router.h"
#include "dispatch.h"
#include "sd_log.h"
#include "pal.h"

#include <stdio.h>
#include <string.h>

static uint32_t g_mode = MODE_STANDALONE;
static uint32_t g_last_gps_poll_ms = 0;
static uint32_t g_last_ag_ms = 0;
static bool     g_session_finalized = false;

void app_on_pps_edge(uint64_t timer_us) {
    pps_time_on_edge(timer_us);
}

static void on_usb_line(const char *line, size_t len, void *ctx) {
    (void)ctx;
    cmd_router_handle_usb_line(line, len);
}

static void detect_mode(void) {
    uint32_t new_mode = pal_usb_cdc_attached() ? MODE_CONNECTED : MODE_STANDALONE;
    if (new_mode == g_mode) return;
    g_mode = new_mode;
    char ev[80];
    snprintf(ev, sizeof(ev), "MODE,%lu,%s\n",
             (unsigned long)pal_time_ms(),
             g_mode == MODE_CONNECTED ? "CONNECTED" : "STANDALONE");
    sd_log_write_event(ev, strlen(ev));
}

static void session_finalize_if_ready(void) {
    if (g_session_finalized) return;
    const gps_state_t *g = gps_state_get();
    if (!g->fix_ok || !g->time_valid) return;

    uint32_t e = g->epoch_s;
    uint32_t day = e / 86400;
    uint32_t sod = e % 86400;
    uint32_t hh = sod / 3600;
    uint32_t mm = (sod % 3600) / 60;
    uint32_t ss = sod % 60;

    uint32_t year = 1970;
    while (1) {
        bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        uint32_t y_days = leap ? 366 : 365;
        if (day < y_days) break;
        day -= y_days;
        year++;
    }
    static const uint8_t dpm[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint32_t month = 1;
    for (; month <= 12; month++) {
        uint32_t d = dpm[month - 1];
        if (month == 2) {
            bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
            if (leap) d = 29;
        }
        if (day < d) break;
        day -= d;
    }
    uint32_t day_num = day + 1;

    char session[40];
    snprintf(session, sizeof(session), "%04lu%02lu%02lu_%02lu%02lu%02lu",
             (unsigned long)year, (unsigned long)month, (unsigned long)day_num,
             (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
    sd_log_set_session(session);
    g_session_finalized = true;
}

static void emit_ag_heartbeat(void) {
    bool tv = pps_time_is_valid();
    const gps_state_t *g = gps_state_get();
    uint32_t err = 0;
    for (uint8_t b = 0; b < N_BRANCH_UARTS; b++) {
        err += branch_uart_lines_bad(b);
    }
    err += dispatch_unknown_prefix();

    char body[160];
    snprintf(body, sizeof(body),
             "$AG,%s,%lu,%u,%u,%u,%u,%u,%u,%lu",
             UNIT_ID_STR,
             (unsigned long)(pal_time_ms() / 1000),
             (unsigned)g_mode,
             (unsigned)(tv ? 1 : 0),
             (unsigned)(g->fix_ok ? 1 : 0),
             (unsigned)g->sat_count,
             (unsigned)(sd_log_ok() ? 1 : 0),
             (unsigned)N_BRANCH_UARTS,
             (unsigned long)err);
    char line[MAX_LINE_LEN];
    size_t n = strlen(body);
    if (n + 4 >= sizeof(line)) return;
    memcpy(line, body, n + 1);
    if (!proto_finalize_line(line, sizeof(line))) return;
    size_t llen = strlen(line);
    usb_cdc_app_send_line(line, llen);
    sd_log_write_event(line, llen);
}

void app_main(void) {
    pal_init();
    pps_time_init();
    gps_init();
    gps_push_config();
    branch_uart_init();
    usb_cdc_app_init();
    sd_log_init();
    dispatch_init();
    dispatch_install();
    usb_cdc_app_register_cb(on_usb_line, NULL);

    pal_pps_register(app_on_pps_edge);

    uint32_t now;
    while (1) {
        pal_iwdg_kick();

        branch_uart_pump();
        usb_cdc_app_pump();

        now = pal_time_ms();
        if (now - g_last_gps_poll_ms >= 100) {
            gps_poll();
            session_finalize_if_ready();
            g_last_gps_poll_ms = now;
        }

        pps_time_emit_tm_if_pending();
        pps_time_health_check();

        if (pps_time_is_valid()) {
            pal_led_set(PAL_LED_TIME_DEGRADED, false);
        } else {
            pal_led_set(PAL_LED_TIME_DEGRADED, true);
        }

        sd_log_tick();

        detect_mode();
        if (g_mode == MODE_CONNECTED) {
            pal_led_set(PAL_LED_BOOT_OK, true);
        } else {
            pal_led_blink_ms(PAL_LED_BOOT_OK, 1000);
        }

        if (now - g_last_ag_ms >= STM32_AG_HB_INTERVAL_MS) {
            emit_ag_heartbeat();
            g_last_ag_ms = now;
        }
    }
}
