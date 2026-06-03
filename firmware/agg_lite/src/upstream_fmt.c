#include "upstream_fmt.h"
#include "proto.h"

#include <stdio.h>
#include <string.h>

static void send_framed(const char *body) {
    char line[MAX_LINE_LEN + 1];
    size_t n = strlen(body);
    if (n + 4 >= sizeof(line)) return;
    memcpy(line, body, n + 1);
    if (!proto_finalize_line(line, sizeof(line))) return;
    record_sink_write(line, strlen(line));
}

static void format_ts(char *out, size_t cap, uint32_t s, uint32_t frac_us) {
    snprintf(out, cap, "%lu.%06lu", (unsigned long)s, (unsigned long)frac_us);
}

static void format_deg(char *out, size_t cap, int32_t e7) {
    /* e7 → decimal degrees with 7 places, sign-aware. */
    int neg = e7 < 0;
    uint32_t mag = (uint32_t)(neg ? -(int64_t)e7 : e7);
    snprintf(out, cap, "%s%lu.%07lu", neg ? "-" : "",
             (unsigned long)(mag / 10000000UL),
             (unsigned long)(mag % 10000000UL));
}

void upstream_emit_wa(uint8_t slot, const dedup_entry_t *e,
                      uint32_t epoch_s, uint32_t frac_us, bool time_valid) {
    if (!e) return;
    char ts[24], bssid[18], ssid_hex[65];
    format_ts(ts, sizeof(ts), epoch_s, frac_us);
    proto_format_mac(e->bssid, bssid, sizeof(bssid));
    if (e->ssid_len == 0) strcpy(ssid_hex, "00");
    else proto_hex_encode(e->ssid, e->ssid_len, ssid_hex, sizeof(ssid_hex));

    char body[MAX_LINE_LEN];
    snprintf(body, sizeof(body),
             "$WA,%s,%s,%s,%s,%d,%u,%u,%u,%s,%u",
             slot_branch_id(slot), ts, bssid, ssid_hex,
             (int)e->best_rssi, (unsigned)e->channel,
             (unsigned)e->enc, (unsigned)e->hidden,
             slot_leaf_id(slot),
             (unsigned)(time_valid ? 0 : 1));
    send_framed(body);
}

void upstream_emit_bd(const ble_dedup_entry_t *e,
                      uint32_t epoch_s, uint32_t frac_us, bool time_valid) {
    if (!e) return;
    char ts[24], bdaddr[18], name_hex[33];
    format_ts(ts, sizeof(ts), epoch_s, frac_us);
    proto_format_mac(e->bdaddr, bdaddr, sizeof(bdaddr));
    if (e->name_len == 0) strcpy(name_hex, "00");
    else proto_hex_encode(e->name, e->name_len, name_hex, sizeof(name_hex));

    /* $BD,BLE,timestamp,bdaddr,addr_type,rssi,channel,adv_type,company_id,
     *     svc_uuid16,name_hex,leaf_id,phy,time_flag  (blebt §10.1, 14 fields) */
    char body[MAX_LINE_LEN];
    snprintf(body, sizeof(body),
             "$BD,%s,%s,%s,%u,%d,%u,%u,%ld,%04X,%s,%s,%u,%u",
             slot_branch_id(SLOT_BLE), ts, bdaddr,
             (unsigned)e->addr_type, (int)e->best_rssi,
             (unsigned)e->channel, (unsigned)e->adv_type,
             (long)e->company_id, (unsigned)e->svc_uuid16,
             name_hex, slot_leaf_id(SLOT_BLE), (unsigned)e->phy,
             (unsigned)(time_valid ? 0 : 1));
    send_framed(body);
}

void upstream_emit_bx(const ble_ext_t *bx,
                      uint32_t epoch_s, uint32_t frac_us, bool time_valid) {
    if (!bx) return;
    char ts[24];
    format_ts(ts, sizeof(ts), epoch_s, frac_us);

    /* $BX,BLE,timestamp,bdaddr,manuf_data_hex,svc_uuid_list_hex,name_full_hex,
     *     time_flag  (blebt §10.2 pass-through, 8 fields) */
    char body[MAX_LINE_LEN];
    snprintf(body, sizeof(body),
             "$BX,%s,%s,%s,%s,%s,%s,%u",
             slot_branch_id(SLOT_BLE), ts, bx->bdaddr,
             bx->manuf_hex[0] ? bx->manuf_hex : "00",
             bx->svc_hex[0] ? bx->svc_hex : "00",
             bx->name_hex[0] ? bx->name_hex : "00",
             (unsigned)(time_valid ? 0 : 1));
    send_framed(body);
}

void upstream_emit_la(const la_fields_t *f) {
    if (!f) return;
    char ts[24], lat[20], lon[20], alt[16];

    if (f->have_time) format_ts(ts, sizeof(ts), f->epoch_s, f->frac_us);
    else              strcpy(ts, "0");

    if (f->fix_ok) {
        format_deg(lat, sizeof(lat), f->lat_e7);
        format_deg(lon, sizeof(lon), f->lon_e7);
        int an = f->alt_cm;
        snprintf(alt, sizeof(alt), "%d.%02d", an / 100, (an < 0 ? -an : an) % 100);
    } else {
        strcpy(lat, "0"); strcpy(lon, "0"); strcpy(alt, "0");
    }

    /* $LA,timestamp,uptime_s,mode,time_valid,fix_ok,sat_count,lat,lon,alt,
     *     pps_age_ms,w24_st,w5g_st,ble_st,sd_ok,sd_kb,q_w24,q_w5g,q_ble,
     *     dedup_w24,dedup_w5g,dedup_ble,err_count  (§7.5, 23 fields) */
    char body[MAX_LINE_LEN];
    snprintf(body, sizeof(body),
             "$LA,%s,%lu,%d,%d,%d,%d,%s,%s,%s,%lu,%d,%d,%d,%d,%lu,%d,%d,%d,%d,%d,%d,%lu",
             ts, (unsigned long)f->uptime_s, f->mode, f->time_valid, f->fix_ok,
             f->sat_count, lat, lon, alt,
             (unsigned long)(f->pps_age_ms > 99999 ? 99999 : f->pps_age_ms),
             f->w24_st, f->w5g_st, f->ble_st, f->sd_ok,
             (unsigned long)f->sd_kb, f->q_w24, f->q_w5g, f->q_ble,
             f->dedup_w24, f->dedup_w5g, f->dedup_ble,
             (unsigned long)f->err_count);
    send_framed(body);
}
