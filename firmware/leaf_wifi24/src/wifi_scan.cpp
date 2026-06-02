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

void start_async(uint8_t channel) {
    if (g_scan_active) return;
    int rc = WiFi.scanNetworks(true /*async*/, true /*show_hidden*/,
                               true /*passive*/, 200 /*max_ms_per_chan*/,
                               channel);
    if (rc < 0 && rc != WIFI_SCAN_RUNNING) {
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
        return;
    }
    g_scan_active = true;
    g_scan_started_ms = millis();
}

bool busy() { return g_scan_active; }

uint32_t consecutive_failures() { return g_consec_fail; }

bool poll_and_emit() {
    if (!g_scan_active) return false;
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return false;
    g_scan_active = false;

    if (n == WIFI_SCAN_FAILED || n < 0) {
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
        return true;
    }
    g_consec_fail = 0;

    uint32_t dur_ms = millis() - g_scan_started_ms;
    int emitted = 0;
    for (int i = 0; i < n; i++) {
        const String &ssid_str = WiFi.SSID(i);
        size_t slen = ssid_str.length();
        if (slen > 32) slen = 32;

        uint8_t ssid_bytes[32];
        if (slen > 0) memcpy(ssid_bytes, ssid_str.c_str(), slen);

        char ssid_hex[65];
        if (slen == 0) {
            ssid_hex[0] = '0';
            ssid_hex[1] = '0';
            ssid_hex[2] = '\0';
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
        int channel = WiFi.channel(i);
        uint8_t enc = map_enc(WiFi.encryptionType(i));
        uint8_t hidden = (slen == 0) ? 1 : 0;

        char body[MAX_LINE_LEN];
        int wrote = snprintf(body, sizeof(body),
                 "$AP,%s,%s,%s,%d,%d,%u,%u",
                 cfg::id_str(), bssid_str, ssid_hex,
                 rssi, channel, (unsigned)enc, (unsigned)hidden);
        if (wrote < 0 || wrote >= (int)sizeof(body) - 4) continue;
        if (uart_proto::send_framed(body)) emitted++;
    }

    char bk_body[64];
    snprintf(bk_body, sizeof(bk_body),
             "$BK,%s,%d,%lu",
             cfg::id_str(), emitted, (unsigned long)dur_ms);
    uart_proto::send_framed(bk_body);

    WiFi.scanDelete();
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

}
