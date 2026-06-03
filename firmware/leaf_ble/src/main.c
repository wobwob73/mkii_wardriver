#include "ble_defs.h"
#include "uart_proto.h"
#include "config.h"
#include "heartbeat.h"
#include "ble_scan.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* --- intra-Leaf dedup set over the current window (blebt §7.5) ---------- */
typedef struct { uint8_t bd[6]; bool used; } seen_t;
static seen_t   g_seen[BLE_SEEN_MAX];
static uint16_t g_seen_count;
static uint32_t g_window_count;
static int64_t  g_window_start_us;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static uint32_t fnv1a(const uint8_t *d, size_t n) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < n; i++) { h ^= d[i]; h *= 0x01000193u; }
    return h;
}

static bool seen_insert(const uint8_t bd[6]) {
    uint32_t start = fnv1a(bd, 6) % BLE_SEEN_MAX;
    for (uint16_t i = 0; i < BLE_SEEN_MAX; i++) {
        uint16_t idx = (start + i) % BLE_SEEN_MAX;
        if (!g_seen[idx].used) {
            memcpy(g_seen[idx].bd, bd, 6);
            g_seen[idx].used = true;
            g_seen_count++;
            return true;            /* newly inserted */
        }
        if (memcmp(g_seen[idx].bd, bd, 6) == 0) return false;   /* duplicate */
    }
    return false;                   /* set full: treat as duplicate (drop) */
}

static void window_reset(void) {
    memset(g_seen, 0, sizeof(g_seen));
    g_seen_count = 0;
    g_window_count = 0;
    g_window_start_us = esp_timer_get_time();
}

static void emit_bl(const BleAdvEvent *e) {
    char bdaddr[18], name_hex[33];
    uart_proto_format_mac(e->bdaddr, bdaddr, sizeof(bdaddr));
    uint8_t nlen = e->name_len > 16 ? 16 : e->name_len;
    if (nlen == 0) strcpy(name_hex, "00");
    else uart_proto_hex_encode(e->name, nlen, name_hex, sizeof(name_hex));

    char body[MAX_LINE_LEN];
    snprintf(body, sizeof(body),
             "$BL,%s,%s,%u,%s,%d,%u,%u,%u,%u,%ld,%04X,%u",
             cfg_id_str(), bdaddr, (unsigned)e->addr_type, name_hex,
             (int)e->rssi, (unsigned)e->channel, (unsigned)e->adv_type,
             (unsigned)e->flags, (unsigned)e->conn,
             (long)e->company_id, (unsigned)e->svc_uuid16, (unsigned)e->phy);
    uart_proto_send_framed(body);
}

static void emit_bx(const BleAdvEvent *e) {
    /* Bound the id into a fixed local so the snprintf below has no unbounded
     * %s (which would trip ESP-IDF's -Werror=format-truncation). */
    char id[8];
    size_t idn = strnlen(cfg_id_str(), sizeof(id) - 1);
    memcpy(id, cfg_id_str(), idn);
    id[idn] = '\0';

    char bdaddr[18];
    char manuf_hex[2 * BX_MANUF_MAX_BYTES + 1];        /* <=61 */
    char svc_hex[2 * BX_SVC_MAX_BYTES + 1];            /* <=49 */
    char name_hex[2 * BX_NAME_EXTRA_MAX_BYTES + 1];    /* <=33 */
    uart_proto_format_mac(e->bdaddr, bdaddr, sizeof(bdaddr));

    /* Combined cap so the UPSTREAM $BX (aggregator prepends branch_id +
     * timestamp + time_flag) still fits MAX_LINE_LEN. Truncation here raises
     * the $HB err_count as the truncation indicator (blebt §4.2). */
    bool trunc = e->lost;

    uint8_t mlen = e->manuf_len;
    if (mlen > BX_MANUF_MAX_BYTES) { mlen = BX_MANUF_MAX_BYTES; trunc = true; }
    if (mlen == 0) strcpy(manuf_hex, "00");
    else uart_proto_hex_encode(e->manuf, mlen, manuf_hex, sizeof(manuf_hex));

    /* svc_list is whole 2-byte UUID tokens; truncate on a token boundary. */
    uint8_t slen = e->svc_list_len;
    if (slen > BX_SVC_MAX_BYTES) { slen = BX_SVC_MAX_BYTES; trunc = true; }
    slen = (uint8_t)(slen & ~1u);
    if (slen == 0) strcpy(svc_hex, "00");
    else uart_proto_hex_encode(e->svc_list, slen, svc_hex, sizeof(svc_hex));

    /* name_full_hex carries only the bytes beyond the 16 already in $BL. */
    if (e->name_len > 16) {
        uint8_t extra = (uint8_t)(e->name_len - 16);
        if (extra > BX_NAME_EXTRA_MAX_BYTES) { extra = BX_NAME_EXTRA_MAX_BYTES; trunc = true; }
        uart_proto_hex_encode(e->name + 16, extra, name_hex, sizeof(name_hex));
    } else {
        strcpy(name_hex, "00");
    }

    char body[MAX_LINE_LEN];
    snprintf(body, sizeof(body), "$BX,%s,%s,%s,%s,%s",
             id, bdaddr, manuf_hex, svc_hex, name_hex);
    uart_proto_send_framed(body);

    if (trunc) hb_note_error();   /* truncation indicator, surfaced via $HB */
}

static void emit_bk(uint32_t window_ms) {
    char body[64];
    snprintf(body, sizeof(body), "$BK,%s,%lu,%lu",
             cfg_id_str(), (unsigned long)g_window_count, (unsigned long)window_ms);
    uart_proto_send_framed(body);
}

/* --- command handling --------------------------------------------------- */
static int parse_int(const char *s, int dflt) {
    if (!s || !*s) return dflt;
    return (int)strtol(s, NULL, 10);
}

static void handle_line(char *line, size_t len) {
    if (!uart_proto_validate_checksum(line, len)) { hb_note_error(); return; }
    char *star = NULL;
    for (size_t i = 1; i < len; i++) { if (line[i] == '*') { star = line + i; break; } }
    if (!star) { hb_note_error(); return; }
    *star = '\0';
    char *f[16];
    int n = uart_proto_split_fields(line + 1, f, 16);
    if (n < 1) { hb_note_error(); return; }

    if (strcmp(f[0], "CF") == 0) {
        if (n < 6) { hb_note_error(); return; }
        if (cfg_adopt(f[1], parse_int(f[2], 0), parse_int(f[3], 3),
                      parse_int(f[4], 1000), parse_int(f[5], 1000))) {
            hb_send_immediate();
        } else {
            hb_note_error();
        }
    } else if (strcmp(f[0], "PG") == 0) {
        if (n < 2) return;
        if (valid_ble_id(f[1]) && strcmp(f[1], cfg_id_str()) == 0) hb_send_immediate();
    } else if (strcmp(f[0], "RB") == 0) {
        if (n < 2) return;
        if (valid_ble_id(f[1]) && strcmp(f[1], cfg_id_str()) == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            esp_restart();
        }
    } else if (strcmp(f[0], "CH") == 0) {
        /* BLE scans all three primary channels concurrently — $CH is dropped
         * (blebt §5.2). */
    } else {
        hb_note_error();
    }
}

void app_main(void) {
    /* NimBLE needs NVS available (PHY calibration / bonding store). */
    esp_err_t nvs_rc = nvs_flash_init();
    if (nvs_rc == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_rc == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    uart_proto_init();
    cfg_init();
    hb_init();

    /* §7.3 runs a continuous 100%-duty ext scan on both PHYs by default; $CF
     * adopts identity (and window/interval, which are advisory under the
     * fixed-duty observer). Start the radio at boot under BLE-?. */
    ble_scan_init(cfg_phy_mask());

    vTaskDelay(pdMS_TO_TICKS(20));
    hb_send_immediate();

    window_reset();
    uint32_t boot_ms = now_ms();
    bool defaults_applied = false;

    char line[MAX_LINE_LEN + 1];
    size_t llen;

    for (;;) {
        /* Service inbound commands. */
        while (uart_proto_poll_line(line, sizeof(line), &llen)) {
            handle_line(line, llen);
        }

        /* Standalone fallback if no $CF within the timeout. */
        if (!cfg_adopted() && !defaults_applied &&
            (now_ms() - boot_ms) >= CONFIG_TIMEOUT_MS) {
            cfg_apply_defaults();
            defaults_applied = true;
            hb_send_immediate();
        }

        /* Drain parsed adv events; one $BL per unique bdaddr per window. */
        BleAdvEvent e;
        while (ble_scan_pop(&e)) {
            if (seen_insert(e.bdaddr)) {
                emit_bl(&e);
                if (e.truncated) emit_bx(&e);
                g_window_count++;
            }
        }

        /* Window boundary → $BK + reset the intra-Leaf dedup set. */
        uint32_t win_ms = (uint32_t)((esp_timer_get_time() - g_window_start_us) / 1000);
        if (win_ms >= BLE_DEDUP_WINDOW_MS) {
            emit_bk(win_ms);
            hb_note_scan_window();
            window_reset();
        }

        if (hb_due()) hb_send();

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
