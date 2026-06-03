#include "upstream_fmt.h"
#include "proto.h"
#include "queues.h"
#include "leaf_health.h"

#include "pico/stdlib.h"
#include "hardware/uart.h"

#include <stdio.h>
#include <string.h>

void upstream_init(void) {
    uart_init(STM32_UART_INST, UPSTREAM_BAUD);
    gpio_set_function(STM32_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(STM32_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(STM32_UART_INST, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(STM32_UART_INST, true);
}

void upstream_send_line(const char *line, size_t len) {
    if (!line || len == 0) return;
    uart_write_blocking(STM32_UART_INST, (const uint8_t *)line, len);
}

static void send_framed(const char *body) {
    char line[MAX_LINE_LEN + 1];
    size_t n = strlen(body);
    if (n + 4 >= sizeof(line)) return;
    memcpy(line, body, n + 1);
    if (!proto_finalize_line(line, sizeof(line))) return;
    upstream_send_line(line, strlen(line));
}

static void format_ts(char *out, size_t cap, uint32_t s, uint32_t frac_us) {
    snprintf(out, cap, "%lu.%06lu", (unsigned long)s, (unsigned long)frac_us);
}

void upstream_emit_wa(const dedup_entry_t *e, uint32_t epoch_s, uint32_t frac_us, bool time_valid) {
    if (!e) return;
    char ts[24], bssid[18], ssid_hex[65];
    format_ts(ts, sizeof(ts), epoch_s, frac_us);
    proto_format_mac(e->bssid, bssid, sizeof(bssid));
    if (e->ssid_len == 0) strcpy(ssid_hex, "00");
    else proto_hex_encode(e->ssid, e->ssid_len, ssid_hex, sizeof(ssid_hex));

    char body[MAX_LINE_LEN];
    snprintf(body, sizeof(body),
             "$WA,%s,%s,%s,%s,%d,%u,%u,%u,%s,%u",
             BRANCH_ID, ts, bssid, ssid_hex,
             (int)e->best_rssi, (unsigned)e->channel,
             (unsigned)e->enc, (unsigned)e->hidden,
             leaf_id_str(e->best_leaf),
             (unsigned)(time_valid ? 0 : 1));
    send_framed(body);
}

void upstream_emit_wp(const wids_event_t *pr, uint32_t epoch_s, uint32_t frac_us, bool time_valid) {
    if (!pr) return;
    char ts[24], src[18], ssid_hex[65];
    format_ts(ts, sizeof(ts), epoch_s, frac_us);
    proto_format_mac(pr->src, src, sizeof(src));
    if (pr->ssid_len == 0) strcpy(ssid_hex, "00");
    else proto_hex_encode(pr->ssid, pr->ssid_len, ssid_hex, sizeof(ssid_hex));

    char body[MAX_LINE_LEN];
    snprintf(body, sizeof(body),
             "$WP,%s,%s,%s,%s,%u,%d,%u",
             BRANCH_ID, ts, src, ssid_hex,
             (unsigned)pr->channel, (int)pr->rssi,
             (unsigned)(time_valid ? 0 : 1));
    send_framed(body);
}

void upstream_emit_et(const evil_twin_alert_t *al, uint32_t epoch_s, uint32_t frac_us, bool time_valid) {
    if (!al) return;
    char ts[24], rb[18], kb[18], ssid_hex[65];
    format_ts(ts, sizeof(ts), epoch_s, frac_us);
    proto_format_mac(al->rogue_bssid, rb, sizeof(rb));
    proto_format_mac(al->known_bssid, kb, sizeof(kb));
    if (al->ssid_len == 0) strcpy(ssid_hex, "00");
    else proto_hex_encode(al->ssid, al->ssid_len, ssid_hex, sizeof(ssid_hex));

    char body[MAX_LINE_LEN];
    snprintf(body, sizeof(body),
             "$ET,%s,%s,%u,%s,%s,%s,%u,%u,%d,%u,%u",
             BRANCH_ID, ts, (unsigned)al->kind, rb, kb, ssid_hex,
             (unsigned)al->rogue_enc, (unsigned)al->known_enc,
             (int)al->rogue_rssi, (unsigned)al->channel,
             (unsigned)(time_valid ? 0 : 1));
    send_framed(body);
}

void upstream_emit_df(const deauth_alert_t *al, uint32_t epoch_s, uint32_t frac_us, bool time_valid) {
    if (!al) return;
    char ts[24], src[18], dst[18];
    format_ts(ts, sizeof(ts), epoch_s, frac_us);
    proto_format_mac(al->src, src, sizeof(src));
    proto_format_mac(al->dst, dst, sizeof(dst));

    char body[MAX_LINE_LEN];
    snprintf(body, sizeof(body),
             "$DF,%s,%s,%s,%s,%u,%u,%u,%u",
             BRANCH_ID, ts, src, dst,
             (unsigned)al->count,
             (unsigned)(DEAUTH_FLOOD_WINDOW_US / 1000000ULL),
             (unsigned)al->channel,
             (unsigned)(time_valid ? 0 : 1));
    send_framed(body);
}

void upstream_emit_bs(uint32_t uptime_s, bool time_valid, bool fix_ok, uint32_t pps_age_ms,
                      uint16_t dedup_count, uint32_t err_count) {
    uint8_t st[N_LEAVES];
    for (uint8_t i = 0; i < N_LEAVES; i++) st[i] = leaf_health_status_code(i);

    char body[MAX_LINE_LEN];
    snprintf(body, sizeof(body),
             "$BS,%s,%lu,%u,%u,%lu,%u,%u,%u,%u,%u,%u,%u,%lu",
             BRANCH_ID, (unsigned long)uptime_s,
             (unsigned)(time_valid ? 1 : 0), (unsigned)(fix_ok ? 1 : 0),
             (unsigned long)(pps_age_ms > 99999 ? 99999 : pps_age_ms),
             (unsigned)st[0], (unsigned)st[1], (unsigned)st[2], (unsigned)st[3],
             (unsigned)detection_q_used(), (unsigned)wids_q_used(),
             (unsigned)dedup_count, (unsigned long)err_count);
    send_framed(body);
}
