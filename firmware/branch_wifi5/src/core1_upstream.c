#include "core1_upstream.h"
#include "proto.h"
#include "queues.h"
#include "dedup.h"
#include "wids.h"
#include "pps_time.h"
#include "upstream_fmt.h"
#include "leaf_cmd.h"

#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/uart.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static line_receiver_t g_stm_rx;
static uint32_t        g_last_window_ms = 0;
static uint32_t        g_last_hb_ms = 0;

static void emit_one_pending(const dedup_entry_t *e, void *ctx) {
    (void)ctx;
    uint32_t epoch_s = 0, frac_us = 0;
    bool time_valid = pps_time_compute(e->local_timer_us, &epoch_s, &frac_us);
    upstream_emit_wa(e, epoch_s, frac_us, time_valid);
    dedup_mark_emitted(e->bssid);
}

static void flush_window(uint64_t now_us) {
    dedup_for_each_pending(emit_one_pending, NULL);
    dedup_advance_window(now_us);
}

static void process_stm32_line(char *line, size_t len) {
    if (!proto_validate_line(line, len)) return;
    char *star = NULL;
    for (size_t i = 1; i < len; i++) {
        if (line[i] == '*') { star = line + i; break; }
    }
    if (!star) return;
    *star = '\0';

    char *fields[16];
    int n = proto_split_body(line + 1, fields, 16);
    if (n < 1) return;

    const char *type = fields[0];
    if (strcmp(type, "TM") == 0) {
        if (n < 3) return;
        uint32_t epoch_s = (uint32_t)strtoul(fields[1], NULL, 10);
        bool fix_ok = atoi(fields[2]) != 0;
        pps_time_apply_tm(epoch_s, fix_ok);
    } else if (strcmp(type, "RQ") == 0) {
        bool tv = pps_time_is_valid();
        bool fo = pps_time_fix_ok();
        uint32_t age = pps_time_age_ms();
        upstream_emit_bs(to_ms_since_boot(get_absolute_time()) / 1000,
                         tv, fo, age,
                         dedup_active_count(),
                         core0_err_count() +
                             detection_q_overflow_count() +
                             wids_q_overflow_count());
    } else if (strcmp(type, "RC") == 0) {
        /*
         * $RC wire format (v1.2 amendment): $RC,<target>,<hex>*<outer_cksum>
         * where <hex> is the ASCII-hex of the full inner framed line including
         * its own '$' and '*XX' (NOT including a trailing '\n'). Hex encoding
         * removes all '*' / ',' collisions with the outer framing — the prior
         * plaintext-nested form was unparseable (outer validator saw the inner
         * '*' as its checksum delimiter and the comma split mangled the inner).
         */
        if (n < 3) return;
        const char *target = fields[1];
        const char *hex_inner = fields[2];
        uint8_t inner_bytes[MAX_LINE_LEN + 1];
        size_t inner_len = proto_hex_decode(hex_inner, inner_bytes,
                                            sizeof(inner_bytes) - 1);
        if (inner_len == 0) return;
        inner_bytes[inner_len] = '\0';
        if (!proto_validate_inner((const char *)inner_bytes, inner_len)) return;
        int idx = leaf_idx_from_id(target);
        if (idx < 0) return;
        char with_nl[MAX_LINE_LEN + 2];
        if (inner_len + 2 > sizeof(with_nl)) return;
        memcpy(with_nl, inner_bytes, inner_len);
        with_nl[inner_len]     = '\n';
        with_nl[inner_len + 1] = '\0';
        leaf_cmd_relay((uint8_t)idx, with_nl);
    }
}

static void service_stm32_rx(void) {
    while (uart_is_readable(STM32_UART_INST)) {
        uint8_t b = (uint8_t)uart_getc(STM32_UART_INST);
        proto_lr_feed(&g_stm_rx, b);
        if (proto_lr_has_line(&g_stm_rx)) {
            char copy[MAX_LINE_LEN + 1];
            size_t llen = g_stm_rx.len;
            if (llen > MAX_LINE_LEN) llen = MAX_LINE_LEN;
            memcpy(copy, g_stm_rx.buf, llen);
            copy[llen] = '\0';
            proto_lr_clear(&g_stm_rx);
            process_stm32_line(copy, llen);
        }
    }
}

void core1_init(void) {
    proto_lr_init(&g_stm_rx);
    g_last_window_ms = to_ms_since_boot(get_absolute_time());
    g_last_hb_ms = g_last_window_ms;
}

void core1_run(void) {
    while (true) {
        service_stm32_rx();
        pps_time_health_check();

        detection_t d;
        while (detection_q_pop(&d)) {
            dedup_update(&d);
        }

        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - g_last_window_ms >= (DEDUP_WINDOW_US / 1000)) {
            flush_window(time_us_64());
            g_last_window_ms = now_ms;
        }

        wids_event_t we;
        while (wids_q_pop(&we)) {
            uint32_t epoch_s = 0, frac_us = 0;
            bool tv = pps_time_compute(we.local_timer_us, &epoch_s, &frac_us);
            if (we.type == WIDS_BEACON) {
                detection_t derived;
                memset(&derived, 0, sizeof(derived));
                derived.local_timer_us = we.local_timer_us;
                derived.leaf_idx = we.leaf_idx;
                memcpy(derived.bssid, we.bssid, 6);
                memcpy(derived.ssid, we.ssid, we.ssid_len);
                derived.ssid_len = we.ssid_len;
                derived.rssi = we.rssi;
                derived.channel = we.channel;
                derived.enc = we.enc;
                derived.hidden = we.hidden;
                evil_twin_alert_t alert;
                if (wids_check_evil_twin(&we, &alert)) {
                    upstream_emit_et(&alert, epoch_s, frac_us, tv);
                }
                dedup_update(&derived);
            } else if (we.type == WIDS_DEAUTH) {
                deauth_alert_t alert;
                if (wids_check_deauth(&we, &alert)) {
                    upstream_emit_df(&alert, epoch_s, frac_us, tv);
                }
            } else if (we.type == WIDS_PROBE) {
                upstream_emit_wp(&we, epoch_s, frac_us, tv);
            }
        }

        if (now_ms - g_last_hb_ms >= BRANCH_HB_INTERVAL_MS) {
            bool tv = pps_time_is_valid();
            bool fo = pps_time_fix_ok();
            uint32_t age = pps_time_age_ms();
            upstream_emit_bs(now_ms / 1000, tv, fo, age,
                             dedup_active_count(),
                             core0_err_count() +
                                 detection_q_overflow_count() +
                                 wids_q_overflow_count());
            g_last_hb_ms = now_ms;
        }

        tight_loop_contents();
    }
}
