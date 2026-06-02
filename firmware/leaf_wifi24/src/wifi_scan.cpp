#include "wifi_scan.h"

#include "uart_proto.h"
#include "config.h"
#include "heartbeat.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <string.h>
#include <stdio.h>

namespace wifi_scan {

static uint32_t g_scan_started_ms = 0;
static bool     g_scan_active = false;
static uint32_t g_consec_fail = 0;

/* Scan-Hop state (mode 2). */
static uint8_t  g_hop_set[14];
static uint8_t  g_hop_len = 0;
static uint8_t  g_cursor = 0;
static uint16_t g_hop_dwell_ms = SCANHOP_DEFAULT_DWELL_MS;
static uint32_t g_sweep_count = 0;
static uint32_t g_sweep_start_ms = 0;
static uint8_t  g_scanhop_emit_channel = 0;   /* channel being swept this scan */

void init() {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(50);
    esp_wifi_set_promiscuous(false);
    g_scan_active = false;
    g_consec_fail = 0;
}

static uint8_t map_enc(wifi_auth_mode_t a) {
    switch (a) {
        case WIFI_AUTH_OPEN:             return LE_OPEN;
        case WIFI_AUTH_WEP:              return LE_WEP;
        case WIFI_AUTH_WPA_PSK:          return LE_WPA_PSK;
        case WIFI_AUTH_WPA2_PSK:         return LE_WPA2_PSK;
        case WIFI_AUTH_WPA_WPA2_PSK:     return LE_WPA_WPA2_PSK;
        case WIFI_AUTH_WPA2_ENTERPRISE:  return LE_WPA2_ENT;
        case WIFI_AUTH_WPA3_PSK:         return LE_WPA3_PSK;
        case WIFI_AUTH_WPA2_WPA3_PSK:    return LE_WPA2_WPA3_PSK;
#ifdef WIFI_AUTH_OWE
        case WIFI_AUTH_OWE:              return LE_OWE;
#endif
#ifdef WIFI_AUTH_WPA3_ENT_192
        case WIFI_AUTH_WPA3_ENT_192:     return LE_WPA3_ENT;
#endif
        default:                         return LE_UNKNOWN;
    }
}

static void handle_scan_fault() {
    g_consec_fail++;
    hb::note_error();
    if (g_consec_fail >= SCAN_FAIL_RESET_THRESHOLD &&
        g_consec_fail % SCAN_FAIL_RESET_THRESHOLD == 0) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_STA);
    }
    if (g_consec_fail >= SCAN_FAIL_REBOOT_THRESHOLD) {
        esp_restart();
    }
}

/* Start an async passive scan on one channel with an explicit dwell. Async is
 * mandatory: a blocking scan stalls the UART RX path and drops commands. */
static void start_async_dwell(uint8_t channel, uint16_t dwell_ms) {
    if (g_scan_active) return;
    int rc = WiFi.scanNetworks(true /*async*/, true /*show_hidden*/,
                               true /*passive*/, (uint32_t)dwell_ms,
                               channel);
    if (rc < 0 && rc != WIFI_SCAN_RUNNING) {
        handle_scan_fault();
        return;
    }
    g_scan_active = true;
    g_scan_started_ms = millis();
}

void start_async(uint8_t channel) {
    /* Parked-scan dwell is the legacy 200 ms/ch. */
    start_async_dwell(channel, 200);
}

bool busy() { return g_scan_active; }

uint32_t consecutive_failures() { return g_consec_fail; }

/* Emit $AP for every result of a completed scan, tagging each with `channel`.
 * Returns the number of $AP emitted, or -1 if the scan is still running, or
 * -2 on scan failure (caller decides whether to emit $BK). On success the
 * result set is deleted before returning. */
static int collect_and_emit_aps(uint8_t channel) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return -1;
    g_scan_active = false;

    if (n == WIFI_SCAN_FAILED || n < 0) {
        handle_scan_fault();
        return -2;
    }
    g_consec_fail = 0;

    int emitted = 0;
    for (int i = 0; i < n; i++) {
        const String &ssid_str = WiFi.SSID(i);
        size_t slen = ssid_str.length();
        if (slen > 32) slen = 32;

        uint8_t ssid_bytes[32];
        if (slen > 0) memcpy(ssid_bytes, ssid_str.c_str(), slen);

        char ssid_hex[65];
        if (slen == 0) {
            ssid_hex[0] = '0'; ssid_hex[1] = '0'; ssid_hex[2] = '\0';
        } else {
            uart_proto::hex_encode(ssid_bytes, slen, ssid_hex, sizeof(ssid_hex));
        }

        uint8_t bssid_buf[6];
        const uint8_t *bptr = WiFi.BSSID(i);
        if (bptr) memcpy(bssid_buf, bptr, 6);
        else      memset(bssid_buf, 0, 6);

        char bssid_str[18];
        uart_proto::format_mac(bssid_buf, bssid_str, sizeof(bssid_str));

        int rssi = WiFi.RSSI(i);
        /* For Scan-Hop the channel field is the channel we were sweeping
         * (passed in); for parked scan WiFi.channel(i) equals it anyway. */
        int ch = (channel != 0) ? channel : WiFi.channel(i);
        uint8_t enc = map_enc(WiFi.encryptionType(i));
        uint8_t hidden = (slen == 0) ? 1 : 0;

        char body[MAX_LINE_LEN];
        int wrote = snprintf(body, sizeof(body),
                 "$AP,%s,%s,%s,%d,%d,%u,%u",
                 cfg::id_str(), bssid_str, ssid_hex,
                 rssi, ch, (unsigned)enc, (unsigned)hidden);
        if (wrote < 0 || wrote >= (int)sizeof(body) - 4) continue;
        if (uart_proto::send_framed(body)) emitted++;
    }

    WiFi.scanDelete();
    return emitted;
}

bool poll_and_emit() {
    if (!g_scan_active) return false;
    uint32_t dur_ms = millis() - g_scan_started_ms;
    int emitted = collect_and_emit_aps(0 /* use WiFi.channel(i) */);
    if (emitted == -1) return false;     /* still running */
    if (emitted == -2) return true;      /* failed; handled */

    char bk_body[64];
    snprintf(bk_body, sizeof(bk_body),
             "$BK,%s,%d,%lu",
             cfg::id_str(), emitted, (unsigned long)dur_ms);
    uart_proto::send_framed(bk_body);
    hb::note_scan_complete();
    return true;
}

void process() {
    if (g_scan_active) {
        poll_and_emit();
    }
    if (!g_scan_active) {
        start_async(cfg::channel());
    }
}

/* --- Scan-Hop ----------------------------------------------------------- */

static void rebuild_hop_set(uint16_t mask) {
    g_hop_len = 0;
    for (uint8_t ch = 1; ch <= 14 && g_hop_len < 14; ch++) {
        if (mask & (1u << (ch - 1))) g_hop_set[g_hop_len++] = ch;
    }
    if (g_hop_len == 0) {
        /* mask=0 is invalid; fall back to the default set rather than stall. */
        g_hop_set[0] = 1; g_hop_set[1] = 6; g_hop_set[2] = 11;
        g_hop_len = 3;
    }
    g_cursor = 0;
    g_sweep_count = 0;
    g_sweep_start_ms = millis();
}

void configure_scanhop(uint16_t mask, uint16_t dwell_ms) {
    g_hop_dwell_ms = (dwell_ms == 0) ? SCANHOP_DEFAULT_DWELL_MS : dwell_ms;
    rebuild_hop_set(mask);
}

void apply_scanhop_mask(uint16_t mask) {
    /* Finish the in-flight scan (do not mutate the hop set under it), then
     * rebuild and reset the cursor — a fresh sweep (§6A.3). If a scan is in
     * flight we let it complete naturally; the next process_scanhop() picks up
     * the new set because we rebuild here and the active scan's results are
     * emitted against g_scanhop_emit_channel captured at start. */
    rebuild_hop_set(mask);
}

void process_scanhop() {
    if (g_hop_len == 0) {
        configure_scanhop(cfg::channel_mask(), cfg::dwell_ms());
    }

    if (g_scan_active) {
        int emitted = collect_and_emit_aps(g_scanhop_emit_channel);
        if (emitted == -1) return;            /* still running */
        if (emitted >= 0) {
            g_sweep_count += (uint32_t)emitted;
            g_cursor = (uint8_t)((g_cursor + 1) % g_hop_len);
            if (g_cursor == 0) {              /* wrapped → sweep complete */
                uint32_t sweep_ms = millis() - g_sweep_start_ms;
                char bk_body[64];
                snprintf(bk_body, sizeof(bk_body),
                         "$BK,%s,%lu,%lu",
                         cfg::id_str(), (unsigned long)g_sweep_count,
                         (unsigned long)sweep_ms);
                uart_proto::send_framed(bk_body);
                hb::note_scan_complete();
                g_sweep_count = 0;
                g_sweep_start_ms = millis();
            }
        }
        /* emitted == -2 (fault) already handled; fall through to restart. */
    }

    if (!g_scan_active) {
        g_scanhop_emit_channel = g_hop_set[g_cursor];
        start_async_dwell(g_scanhop_emit_channel, g_hop_dwell_ms);
    }
}

}
