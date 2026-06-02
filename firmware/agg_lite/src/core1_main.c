#include "core1_main.h"
#include "agg_defs.h"
#include "agg_state.h"
#include "gps.h"
#include "pps_time.h"
#include "queues.h"
#include "dedup_wifi.h"
#include "dedup_ble.h"
#include "upstream_fmt.h"
#include "sd_log.h"
#include "usb_cdc_mirror.h"
#include "core0_leaf_io.h"
#include "leaf_health.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"

#include <string.h>
#include <stdio.h>

typedef enum {
    PHASE_WAIT_TIME = 0,   /* waiting for PPS + GPS UTC second (or 20 s timeout) */
    PHASE_RUNNING   = 1,   /* leaves configured; ingest + log (pending→named)    */
} agg_phase_t;

static agg_phase_t   g_phase;
static uint32_t      g_phase_start_ms;

static wifi_dedup_t  g_dd[N_WIFI_FAMILIES];   /* indexed by SLOT_W24 / SLOT_W5G */
static ble_dedup_t   g_ble_dd;

static uint32_t g_last_wifi_flush_ms;
static uint32_t g_last_ble_flush_ms;
static uint32_t g_last_la_ms;
static uint32_t g_last_led_ms;
static bool     g_led_on;

/* ---- record sink: SD ring (durable) + CDC mirror (live) --------------- */
void record_sink_write(const char *line, size_t len) {
    sd_log_enqueue(line, len);
    usb_cdc_mirror_write(line, len);
}

/* ---- WiFi dedup flush -------------------------------------------------- */
typedef struct { uint8_t slot; wifi_dedup_t *dd; } wifi_flush_ctx_t;

static void emit_wifi_pending(const dedup_entry_t *e, void *vctx) {
    wifi_flush_ctx_t *ctx = (wifi_flush_ctx_t *)vctx;
    uint32_t epoch_s = 0, frac_us = 0;
    bool tv = pps_time_compute(e->local_timer_us, &epoch_s, &frac_us);
    upstream_emit_wa(ctx->slot, e, epoch_s, frac_us, tv);
    wifi_dedup_mark_emitted(ctx->dd, e->bssid);
}

static void flush_wifi(uint8_t slot, uint64_t now_us) {
    wifi_flush_ctx_t ctx = { slot, &g_dd[slot] };
    wifi_dedup_for_each_pending(&g_dd[slot], emit_wifi_pending, &ctx);
    wifi_dedup_advance_window(&g_dd[slot], now_us);
}

/* ---- BLE dedup flush --------------------------------------------------- */
static void emit_ble_pending(const ble_dedup_entry_t *e, void *vctx) {
    (void)vctx;
    uint32_t epoch_s = 0, frac_us = 0;
    bool tv = pps_time_compute(e->local_timer_us, &epoch_s, &frac_us);
    upstream_emit_bd(e, epoch_s, frac_us, tv);
    ble_dedup_mark_emitted(&g_ble_dd, e->bdaddr);
}

static void flush_ble(uint64_t now_us) {
    ble_dedup_for_each_pending(&g_ble_dd, emit_ble_pending, NULL);
    ble_dedup_advance_window(&g_ble_dd, now_us);
}

/* ---- epoch → YYYYMMDD_HHMMSS session id -------------------------------- */
static void epoch_to_session(uint32_t epoch, char *out, size_t cap) {
    uint32_t days = epoch / 86400u;
    uint32_t secs = epoch % 86400u;
    uint32_t hh = secs / 3600u;
    uint32_t mm = (secs % 3600u) / 60u;
    uint32_t ss = secs % 60u;

    int year = 1970;
    for (;;) {
        bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        uint32_t dy = leap ? 366u : 365u;
        if (days < dy) break;
        days -= dy;
        year++;
    }
    static const int dpm[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int month = 1;
    for (; month <= 12; month++) {
        uint32_t dm = (uint32_t)dpm[month - 1];
        if (month == 2) {
            bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
            if (leap) dm = 29;
        }
        if (days < dm) break;
        days -= dm;
    }
    uint32_t day = days + 1;
    snprintf(out, cap, "%04d%02d%02d_%02lu%02lu%02lu",
             year, month, (int)day,
             (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
}

static void maybe_open_session(void) {
    if (sd_log_has_session()) return;
    if (!sd_log_mounted()) return;
    const gps_state_t *g = gps_state_get();
    /* Fix gate (§10): first 3D fix with HDOP < 5 opens the named session. */
    if (!g->fix_ok) return;
    if (g->hdop_x10 == 0 || g->hdop_x10 >= FIX_GATE_HDOP_X10_MAX) return;
    if (g->epoch_s == 0) return;
    char id[20];
    epoch_to_session(g->epoch_s, id, sizeof(id));
    sd_log_set_session(id);
}

/* ---- $LA heartbeat ----------------------------------------------------- */
static void emit_la(void) {
    const gps_state_t *g = gps_state_get();
    uint32_t epoch_s = 0, frac_us = 0;
    bool have_time = pps_time_compute(time_us_64(), &epoch_s, &frac_us);

    la_fields_t f;
    memset(&f, 0, sizeof(f));
    f.have_time  = have_time;
    f.epoch_s    = epoch_s;
    f.frac_us    = frac_us;
    f.uptime_s   = to_ms_since_boot(get_absolute_time()) / 1000u;
    f.mode       = usb_cdc_mirror_connected() ? 1 : 0;
    f.time_valid = pps_time_is_valid() ? 1 : 0;
    f.fix_ok     = g->fix_ok ? 1 : 0;
    f.sat_count  = g->sat_count;
    f.lat_e7     = g->lat_e7;
    f.lon_e7     = g->lon_e7;
    f.alt_cm     = g->alt_cm;
    f.pps_age_ms = pps_time_age_ms();
    f.w24_st     = leaf_health_status_code(SLOT_W24);
    f.w5g_st     = leaf_health_status_code(SLOT_W5G);
    f.ble_st     = leaf_health_status_code(SLOT_BLE);
    f.sd_ok      = sd_log_mounted() ? 1 : 0;
    f.sd_kb      = sd_log_kb_written();
    f.q_w24      = wifi_det_q_used(SLOT_W24);
    f.q_w5g      = wifi_det_q_used(SLOT_W5G);
    f.q_ble      = ble_q_used();
    f.dedup_w24  = wifi_dedup_active_count(&g_dd[SLOT_W24]);
    f.dedup_w5g  = wifi_dedup_active_count(&g_dd[SLOT_W5G]);
    f.dedup_ble  = ble_dedup_active_count(&g_ble_dd);
    f.err_count  = core0_err_count() + queues_overflow_count()
                 + sd_log_drop_count() + sd_log_error_count();
    upstream_emit_la(&f);
}

static void service_led(uint32_t now) {
    /* Standalone w/o SD: fast blink (250 ms). Otherwise slow blink (1 s). */
    uint32_t period = sd_log_mounted() ? 1000u : 250u;
    if (now - g_last_led_ms >= period) {
        g_led_on = !g_led_on;
        gpio_put(AGG_LED_PIN, g_led_on);
        g_last_led_ms = now;
    }
}

void core1_init(void) {
    gps_init();
    sd_log_init();
    usb_cdc_mirror_init();

    for (int f = 0; f < N_WIFI_FAMILIES; f++) wifi_dedup_init(&g_dd[f]);
    ble_dedup_init(&g_ble_dd);

    uint32_t now = to_ms_since_boot(get_absolute_time());
    g_phase = PHASE_WAIT_TIME;
    g_phase_start_ms = now;
    g_last_wifi_flush_ms = now;
    g_last_ble_flush_ms = now;
    g_last_la_ms = now;
    g_last_led_ms = now;
    g_led_on = false;
}

void core1_run(void) {
    while (true) {
        gps_poll();
        pps_time_health_check();

        uint32_t now = to_ms_since_boot(get_absolute_time());

        if (g_phase == PHASE_WAIT_TIME) {
            if (pps_time_is_valid() ||
                (now - g_phase_start_ms >= WAIT_TIME_TIMEOUT_MS)) {
                agg_request_leaf_init();   /* INIT_LEAVES: Core 0 sends $CF */
                g_phase = PHASE_RUNNING;
            }
        }

        /* Ingest detections into the per-family dedup tables. */
        detection_t d;
        for (uint8_t fam = 0; fam < N_WIFI_FAMILIES; fam++) {
            while (wifi_det_q_pop(fam, &d)) wifi_dedup_update(&g_dd[fam], &d);
        }
        ble_event_t be;
        while (ble_q_pop(&be)) ble_dedup_update(&g_ble_dd, &be);

        /* $BX is pass-through (no dedup): re-frame with branch_id + ts. */
        ble_ext_t bx;
        while (ble_ext_q_pop(&bx)) {
            uint32_t es = 0, fu = 0;
            bool tv = pps_time_compute(bx.local_timer_us, &es, &fu);
            upstream_emit_bx(&bx, es, fu, tv);
        }

        if (now - g_last_wifi_flush_ms >= (WIFI_DEDUP_WINDOW_US / 1000)) {
            uint64_t now_us = time_us_64();
            for (uint8_t fam = 0; fam < N_WIFI_FAMILIES; fam++) flush_wifi(fam, now_us);
            g_last_wifi_flush_ms = now;
        }
        if (now - g_last_ble_flush_ms >= (BLE_DEDUP_WINDOW_US / 1000)) {
            flush_ble(time_us_64());
            g_last_ble_flush_ms = now;
        }

        maybe_open_session();
        sd_log_tick();

        if (now - g_last_la_ms >= AGG_HB_INTERVAL_MS) {
            emit_la();
            g_last_la_ms = now;
        }

        service_led(now);
        tight_loop_contents();
    }
}
