#include "wids_monitor.h"

#include "uart_proto.h"
#include "config.h"
#include "heartbeat.h"
#include "frame_parser.h"
#include "bssid_tracker.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <string.h>
#include <stdio.h>

namespace wids_monitor {

static volatile uint16_t g_w_idx = 0;
static volatile uint16_t g_r_idx = 0;
static volatile uint32_t g_dropped = 0;
static WidsEvent g_ring[WIDS_RING_SIZE];

static uint8_t  g_chan_list[14];
static uint8_t  g_chan_count = 0;
static uint8_t  g_chan_idx = 0;
static uint16_t g_dwell_ms = 100;
static uint32_t g_dwell_start_ms = 0;
static bool     g_running = false;

static void build_channel_list(uint16_t mask) {
    g_chan_count = 0;
    for (uint8_t b = 0; b < 14; b++) {
        if (mask & (1u << b)) {
            g_chan_list[g_chan_count++] = b + 1;
        }
    }
    g_chan_idx = 0;
    if (g_chan_count == 0) {
        g_chan_list[0] = 1;
        g_chan_count = 1;
    }
}

static inline uint16_t next_idx(uint16_t i) {
    return (uint16_t)((i + 1) % WIDS_RING_SIZE);
}

static void IRAM_ATTR rx_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    const uint8_t *frame = pkt->payload;
    uint32_t flen = pkt->rx_ctrl.sig_len;
    if (flen < 24) return;

    uint16_t w = g_w_idx;
    uint16_t r = g_r_idx;
    if (next_idx(w) == r) {
        g_dropped++;
        return;
    }

    WidsEvent &e = g_ring[w];
    memset(&e, 0, sizeof(e));
    uint8_t subtype = frame_parser::frame_subtype(frame, flen);
    e.subtype = subtype;
    e.rssi = pkt->rx_ctrl.rssi;
    e.channel = pkt->rx_ctrl.channel;

    frame_parser::MgmtAddrs addrs;
    if (!frame_parser::decode_mgmt_addrs(frame, flen, addrs)) return;
    memcpy(e.dst,   addrs.dst,   6);
    memcpy(e.src,   addrs.src,   6);
    memcpy(e.bssid, addrs.bssid, 6);

    if (subtype == 0xA0 || subtype == 0xC0) {
        e.type = EVENT_DEAUTH;
        uint16_t reason = 0;
        frame_parser::decode_reason_code(frame, flen, reason);
        e.reason = reason;
    } else if (subtype == 0x40) {
        e.type = EVENT_PROBE;
        uint8_t slen = 0;
        frame_parser::parse_probe_request(frame, flen, e.ssid, slen);
        e.ssid_len = slen;
    } else if (subtype == 0x80) {
        e.type = EVENT_BEACON;
        uint8_t slen = 0;
        uint8_t enc = LE_UNKNOWN;
        uint8_t hidden = 0;
        frame_parser::parse_beacon(frame, flen, e.ssid, slen, enc, hidden);
        e.ssid_len = slen;
        e.enc = enc;
        e.hidden = hidden;
    } else {
        return;
    }

    g_w_idx = next_idx(w);
}

void init() {
    g_w_idx = 0;
    g_r_idx = 0;
    g_dropped = 0;
    g_running = false;
    g_dwell_ms = 100;
    g_chan_count = 0;
    g_chan_idx = 0;
    bssid_tracker::init();
}

static void install_filter_and_start() {
    wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(rx_cb);
    esp_wifi_set_promiscuous(true);
}

void start() {
    if (g_running) return;
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(50);
    g_dwell_ms = cfg::dwell_ms() == 0 ? 100 : cfg::dwell_ms();
    build_channel_list(cfg::channel_mask());
    install_filter_and_start();
    esp_wifi_set_channel(g_chan_list[g_chan_idx], WIFI_SECOND_CHAN_NONE);
    g_dwell_start_ms = millis();
    g_running = true;
}

void stop() {
    if (!g_running) return;
    esp_wifi_set_promiscuous(false);
    g_running = false;
}

void apply_channel_mask(uint16_t mask) {
    build_channel_list(mask);
    if (g_running) {
        esp_wifi_set_channel(g_chan_list[g_chan_idx], WIFI_SECOND_CHAN_NONE);
        g_dwell_start_ms = millis();
    }
}

uint32_t dropped_count() { return g_dropped; }

static void drain_ring() {
    while (g_r_idx != g_w_idx) {
        WidsEvent &e = g_ring[g_r_idx];

        if (e.type == EVENT_BEACON) {
            if (bssid_tracker::seen_or_insert(e.bssid)) {
                g_r_idx = next_idx(g_r_idx);
                continue;
            }
            char bssid_str[18];
            uart_proto::format_mac(e.bssid, bssid_str, sizeof(bssid_str));
            char ssid_hex[65];
            if (e.ssid_len == 0) {
                ssid_hex[0] = '0';
                ssid_hex[1] = '0';
                ssid_hex[2] = '\0';
            } else {
                uart_proto::hex_encode(e.ssid, e.ssid_len, ssid_hex, sizeof(ssid_hex));
            }
            char body[MAX_LINE_LEN];
            snprintf(body, sizeof(body),
                     "$BC,%s,%s,%s,%d,%u,%u,%u",
                     cfg::id_str(), bssid_str, ssid_hex,
                     (int)e.rssi, (unsigned)e.channel,
                     (unsigned)e.enc, (unsigned)e.hidden);
            uart_proto::send_framed(body);
        } else if (e.type == EVENT_DEAUTH) {
            char src_str[18], dst_str[18];
            uart_proto::format_mac(e.src, src_str, sizeof(src_str));
            uart_proto::format_mac(e.dst, dst_str, sizeof(dst_str));
            char body[MAX_LINE_LEN];
            snprintf(body, sizeof(body),
                     "$DE,%s,%u,%s,%s,%u,%u,%d",
                     cfg::id_str(), (unsigned)e.subtype,
                     src_str, dst_str, (unsigned)e.reason,
                     (unsigned)e.channel, (int)e.rssi);
            uart_proto::send_framed(body);
        } else if (e.type == EVENT_PROBE) {
            char src_str[18];
            uart_proto::format_mac(e.src, src_str, sizeof(src_str));
            char ssid_hex[65];
            if (e.ssid_len == 0) {
                ssid_hex[0] = '0';
                ssid_hex[1] = '0';
                ssid_hex[2] = '\0';
            } else {
                uart_proto::hex_encode(e.ssid, e.ssid_len, ssid_hex, sizeof(ssid_hex));
            }
            char body[MAX_LINE_LEN];
            snprintf(body, sizeof(body),
                     "$PR,%s,%s,%s,%u,%d",
                     cfg::id_str(), src_str, ssid_hex,
                     (unsigned)e.channel, (int)e.rssi);
            uart_proto::send_framed(body);
        }

        g_r_idx = next_idx(g_r_idx);
    }
}

void process() {
    if (!g_running) return;
    drain_ring();
    uint32_t now = millis();
    if ((now - g_dwell_start_ms) >= g_dwell_ms) {
        g_chan_idx = (uint8_t)((g_chan_idx + 1) % g_chan_count);
        if (g_chan_idx == 0) {
            bssid_tracker::reset_cycle();
            hb::note_scan_complete();
        }
        esp_wifi_set_channel(g_chan_list[g_chan_idx], WIFI_SECOND_CHAN_NONE);
        g_dwell_start_ms = now;
    }
}

}
