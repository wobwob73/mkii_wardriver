#include "core0_leaf_io.h"
#include "pio_uart.h"
#include "proto.h"
#include "queues.h"
#include "leaf_health.h"
#include "leaf_cmd.h"

#include "pico/stdlib.h"
#include "pico/time.h"

#include <string.h>
#include <stdlib.h>

#define RX_RING_WORDS 128

static pio_uart_rx_t g_rx[N_LEAVES];
static uint32_t      g_ring[N_LEAVES][RX_RING_WORDS];
static line_receiver_t g_lr[N_LEAVES];

static uint32_t g_err_total = 0;
static uint32_t g_last_watchdog_ms = 0;

static const uint8_t g_rx_pins[N_LEAVES] = {
    LEAF_W1_RX_PIN, LEAF_W2_RX_PIN, LEAF_W3_RX_PIN, LEAF_W4_RX_PIN
};

static void dispatch_message(uint8_t leaf_idx, char *line, size_t len) {
    if (!proto_validate_line(line, len)) {
        g_err_total++;
        return;
    }
    char *star = NULL;
    for (size_t i = 1; i < len; i++) {
        if (line[i] == '*') { star = line + i; break; }
    }
    if (!star) { g_err_total++; return; }
    *star = '\0';

    char *fields[16];
    int n = proto_split_body(line + 1, fields, 16);
    if (n < 1) { g_err_total++; return; }

    const char *type = fields[0];
    uint64_t now_us = time_us_64();

    if (strcmp(type, "AP") == 0) {
        if (n < 8) { g_err_total++; return; }
        detection_t d;
        memset(&d, 0, sizeof(d));
        d.local_timer_us = now_us;
        d.leaf_idx = leaf_idx;
        if (!proto_parse_mac(fields[2], d.bssid)) { g_err_total++; return; }
        size_t hlen = strlen(fields[3]);
        if (hlen == 2 && fields[3][0] == '0' && fields[3][1] == '0') {
            d.ssid_len = 0;
        } else {
            size_t bl = proto_hex_decode(fields[3], d.ssid, sizeof(d.ssid));
            d.ssid_len = (uint8_t)bl;
        }
        d.rssi = (int8_t)atoi(fields[4]);
        d.channel = (uint8_t)atoi(fields[5]);
        d.enc = (uint8_t)atoi(fields[6]);
        if (d.enc > 10) d.enc = LE_UNKNOWN;
        d.hidden = (uint8_t)atoi(fields[7]);
        if (!detection_q_push(&d)) g_err_total++;
        leaf_health_note_ap_received(leaf_idx);
    } else if (strcmp(type, "BK") == 0) {
        if (n < 4) { g_err_total++; return; }
        uint32_t expected = (uint32_t)atol(fields[2]);
        leaf_state_t *l = leaf_health_get(leaf_idx);
        leaf_health_note_bk(leaf_idx, expected, l ? l->ap_received : 0);
    } else if (strcmp(type, "DE") == 0) {
        if (n < 8) { g_err_total++; return; }
        wids_event_t e;
        memset(&e, 0, sizeof(e));
        e.local_timer_us = now_us;
        e.type = WIDS_DEAUTH;
        e.leaf_idx = leaf_idx;
        e.subtype = (uint8_t)atoi(fields[2]);
        if (!proto_parse_mac(fields[3], e.src)) { g_err_total++; return; }
        if (!proto_parse_mac(fields[4], e.dst)) { g_err_total++; return; }
        e.reason = (uint16_t)atoi(fields[5]);
        e.channel = (uint8_t)atoi(fields[6]);
        e.rssi = (int8_t)atoi(fields[7]);
        if (!wids_q_push(&e)) g_err_total++;
    } else if (strcmp(type, "PR") == 0) {
        if (n < 6) { g_err_total++; return; }
        wids_event_t e;
        memset(&e, 0, sizeof(e));
        e.local_timer_us = now_us;
        e.type = WIDS_PROBE;
        e.leaf_idx = leaf_idx;
        if (!proto_parse_mac(fields[2], e.src)) { g_err_total++; return; }
        size_t hlen = strlen(fields[3]);
        if (!(hlen == 2 && fields[3][0] == '0' && fields[3][1] == '0')) {
            size_t bl = proto_hex_decode(fields[3], e.ssid, sizeof(e.ssid));
            e.ssid_len = (uint8_t)bl;
        }
        e.channel = (uint8_t)atoi(fields[4]);
        e.rssi = (int8_t)atoi(fields[5]);
        if (!wids_q_push(&e)) g_err_total++;
    } else if (strcmp(type, "BC") == 0) {
        if (n < 8) { g_err_total++; return; }
        wids_event_t e;
        memset(&e, 0, sizeof(e));
        e.local_timer_us = now_us;
        e.type = WIDS_BEACON;
        e.leaf_idx = leaf_idx;
        if (!proto_parse_mac(fields[2], e.bssid)) { g_err_total++; return; }
        size_t hlen = strlen(fields[3]);
        if (!(hlen == 2 && fields[3][0] == '0' && fields[3][1] == '0')) {
            size_t bl = proto_hex_decode(fields[3], e.ssid, sizeof(e.ssid));
            e.ssid_len = (uint8_t)bl;
        }
        e.rssi = (int8_t)atoi(fields[4]);
        e.channel = (uint8_t)atoi(fields[5]);
        e.enc = (uint8_t)atoi(fields[6]);
        if (e.enc > 10) e.enc = LE_UNKNOWN;
        e.hidden = (uint8_t)atoi(fields[7]);
        if (!wids_q_push(&e)) g_err_total++;
    } else if (strcmp(type, "HB") == 0) {
        if (n < 6) { g_err_total++; return; }
        uint32_t uptime_s = (uint32_t)atol(fields[2]);
        uint32_t free_heap = (uint32_t)atol(fields[3]);
        uint32_t scan_count = (uint32_t)atol(fields[4]);
        uint32_t err_count = (uint32_t)atol(fields[5]);
        leaf_health_note_hb(leaf_idx, uptime_s, free_heap, scan_count, err_count);
    } else {
        g_err_total++;
    }
}

void core0_init(void) {
    g_err_total = 0;
    g_last_watchdog_ms = to_ms_since_boot(get_absolute_time());

    for (uint8_t i = 0; i < N_LEAVES; i++) {
        pio_uart_rx_init(&g_rx[i], g_rx_pins[i], i, g_ring[i], RX_RING_WORDS);
        proto_lr_init(&g_lr[i]);
    }

    sleep_ms(LEAF_BOOT_DELAY_MS);
    for (uint8_t i = 0; i < N_LEAVES; i++) {
        leaf_state_t *l = leaf_health_get(i);
        if (l) {
            leaf_cmd_send_cf(i);
            l->configured = false;
            l->last_cf_sent_ms = to_ms_since_boot(get_absolute_time());
            l->cf_retries = 0;
        }
    }
}

uint32_t core0_err_count(void) { return g_err_total; }

void core0_run(void) {
    while (true) {
        for (uint8_t i = 0; i < N_LEAVES; i++) {
            uint8_t buf[64];
            int n = pio_uart_rx_read(&g_rx[i], buf, sizeof(buf));
            for (int b = 0; b < n; b++) {
                proto_lr_feed(&g_lr[i], buf[b]);
                if (proto_lr_has_line(&g_lr[i])) {
                    size_t llen = g_lr[i].len;
                    char copy[MAX_LINE_LEN + 1];
                    if (llen > MAX_LINE_LEN) llen = MAX_LINE_LEN;
                    memcpy(copy, g_lr[i].buf, llen);
                    copy[llen] = '\0';
                    proto_lr_clear(&g_lr[i]);
                    dispatch_message(i, copy, llen);
                }
            }
        }
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - g_last_watchdog_ms >= 1000) {
            leaf_health_tick_1s();
            g_last_watchdog_ms = now;
        }
        tight_loop_contents();
    }
}
