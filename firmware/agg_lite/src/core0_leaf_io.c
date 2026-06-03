#include "core0_leaf_io.h"
#include "pio_uart.h"
#include "proto.h"
#include "queues.h"
#include "leaf_health.h"
#include "leaf_cmd.h"
#include "agg_state.h"

#include "pico/stdlib.h"
#include "pico/time.h"

#include <string.h>
#include <stdlib.h>

#define RX_RING_WORDS 128

static pio_uart_rx_t   g_rx[N_LEAVES];
static uint32_t        g_ring[N_LEAVES][RX_RING_WORDS];
static line_receiver_t g_lr[N_LEAVES];

static uint32_t g_err_total = 0;
static uint32_t g_last_watchdog_ms = 0;
static bool     g_cf_sent = false;

static const uint8_t g_rx_pins[N_LEAVES] = {
    LEAF_W24_RX_PIN, LEAF_W5G_RX_PIN, LEAF_BLE_RX_PIN
};

/* --- per-family line dispatch ------------------------------------------ */

static void dispatch_wifi(uint8_t slot, char **fields, int n, uint64_t now_us) {
    const char *type = fields[0];
    if (strcmp(type, "AP") == 0) {
        if (n < 8) { g_err_total++; return; }
        detection_t d;
        memset(&d, 0, sizeof(d));
        d.local_timer_us = now_us;
        d.leaf_idx = slot;
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
        if (!wifi_det_q_push(slot, &d)) g_err_total++;
        leaf_health_note_ap_received(slot);
    } else if (strcmp(type, "BK") == 0) {
        if (n < 4) { g_err_total++; return; }
        uint32_t expected = (uint32_t)atol(fields[2]);
        leaf_state_t *l = leaf_health_get(slot);
        leaf_health_note_bk(slot, expected, l ? l->ap_received : 0);
    } else if (strcmp(type, "HB") == 0) {
        if (n < 6) { g_err_total++; return; }
        leaf_health_note_hb(slot,
                            (uint32_t)atol(fields[2]), (uint32_t)atol(fields[3]),
                            (uint32_t)atol(fields[4]), (uint32_t)atol(fields[5]));
    } else {
        /* Scan-hop / 5 GHz scan Leaves emit no WIDS-class lines; anything else
         * is unexpected on a WiFi slot. */
        g_err_total++;
    }
}

static void dispatch_ble(char **fields, int n, uint64_t now_us) {
    const char *type = fields[0];
    if (strcmp(type, "BL") == 0) {
        if (n < 13) { g_err_total++; return; }
        ble_event_t e;
        memset(&e, 0, sizeof(e));
        e.local_timer_us = now_us;
        if (!proto_parse_mac(fields[2], e.bdaddr)) { g_err_total++; return; }
        e.addr_type = (uint8_t)atoi(fields[3]);
        size_t hlen = strlen(fields[4]);
        if (!(hlen == 2 && fields[4][0] == '0' && fields[4][1] == '0')) {
            size_t bl = proto_hex_decode(fields[4], e.name, sizeof(e.name));
            e.name_len = (uint8_t)bl;
        }
        e.rssi = (int8_t)atoi(fields[5]);
        e.channel = (uint8_t)atoi(fields[6]);
        e.adv_type = (uint8_t)atoi(fields[7]);
        e.flags = (uint8_t)atoi(fields[8]);
        e.conn = (uint8_t)atoi(fields[9]);
        e.company_id = (int32_t)atol(fields[10]);
        e.svc_uuid16 = (uint16_t)strtoul(fields[11], NULL, 16);
        e.phy = (uint8_t)atoi(fields[12]);
        if (!ble_q_push(&e)) g_err_total++;
        leaf_health_note_ap_received(SLOT_BLE);
    } else if (strcmp(type, "BX") == 0) {
        if (n < 6) { g_err_total++; return; }
        ble_ext_t bx;
        memset(&bx, 0, sizeof(bx));
        bx.local_timer_us = now_us;
        strncpy(bx.bdaddr, fields[2], sizeof(bx.bdaddr) - 1);
        strncpy(bx.manuf_hex, fields[3], sizeof(bx.manuf_hex) - 1);
        strncpy(bx.svc_hex, fields[4], sizeof(bx.svc_hex) - 1);
        strncpy(bx.name_hex, fields[5], sizeof(bx.name_hex) - 1);
        if (!ble_ext_q_push(&bx)) g_err_total++;
    } else if (strcmp(type, "BK") == 0) {
        if (n < 4) { g_err_total++; return; }
        uint32_t expected = (uint32_t)atol(fields[2]);
        leaf_state_t *l = leaf_health_get(SLOT_BLE);
        leaf_health_note_bk(SLOT_BLE, expected, l ? l->ap_received : 0);
    } else if (strcmp(type, "HB") == 0) {
        if (n < 6) { g_err_total++; return; }
        leaf_health_note_hb(SLOT_BLE,
                            (uint32_t)atol(fields[2]), (uint32_t)atol(fields[3]),
                            (uint32_t)atol(fields[4]), (uint32_t)atol(fields[5]));
    } else {
        g_err_total++;
    }
}

static void dispatch_message(uint8_t slot, char *line, size_t len) {
    if (!proto_validate_line(line, len)) { g_err_total++; return; }
    char *star = NULL;
    for (size_t i = 1; i < len; i++) {
        if (line[i] == '*') { star = line + i; break; }
    }
    if (!star) { g_err_total++; return; }
    *star = '\0';

    char *fields[16];
    int n = proto_split_body(line + 1, fields, 16);
    if (n < 1) { g_err_total++; return; }

    uint64_t now_us = time_us_64();
    if (slot == SLOT_BLE) dispatch_ble(fields, n, now_us);
    else                  dispatch_wifi(slot, fields, n, now_us);
}

void core0_init(void) {
    g_err_total = 0;
    g_cf_sent = false;
    g_last_watchdog_ms = to_ms_since_boot(get_absolute_time());

    for (uint8_t i = 0; i < N_LEAVES; i++) {
        pio_uart_rx_init(&g_rx[i], g_rx_pins[i], i, g_ring[i], RX_RING_WORDS);
        proto_lr_init(&g_lr[i]);
    }
}

uint32_t core0_err_count(void) { return g_err_total; }

static void send_initial_cf(void) {
    sleep_ms(LEAF_BOOT_DELAY_MS);
    uint32_t now = to_ms_since_boot(get_absolute_time());
    for (uint8_t i = 0; i < N_LEAVES; i++) {
        leaf_state_t *l = leaf_health_get(i);
        if (l) {
            leaf_cmd_send_cf(i);
            l->configured = false;
            l->last_cf_sent_ms = now;
            l->cf_retries = 0;
        }
    }
    g_cf_sent = true;
}

void core0_run(void) {
    while (true) {
        /* §10: hold Leaf $CF until Core 1 has crossed WAIT_TIME (INIT_LEAVES). */
        if (!g_cf_sent && agg_leaf_init_requested()) {
            send_initial_cf();
        }

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
        if (g_cf_sent && (now - g_last_watchdog_ms >= 1000)) {
            leaf_health_tick_1s();
            g_last_watchdog_ms = now;
        }
        tight_loop_contents();
    }
}
