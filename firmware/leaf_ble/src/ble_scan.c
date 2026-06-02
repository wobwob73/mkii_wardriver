#include "ble_scan.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nimble/hci_common.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_hs_adv.h"
#include "host/util/util.h"

#include <string.h>

static const char *TAG = "ble_scan";

static QueueHandle_t g_q;
static uint8_t       g_own_addr_type;
static uint8_t       g_phy_mask = 3;
static volatile bool g_running = false;

static void start_scan(void);

/* ble random-address sub-type from the top two bits of the MSB (Core spec). */
static uint8_t classify_addr(const ble_addr_t *a) {
    if (a->type == BLE_ADDR_PUBLIC || a->type == BLE_ADDR_PUBLIC_ID) return 0;
    uint8_t top = a->val[5] & 0xC0;
    if (top == 0xC0) return 1;       /* static random */
    if (top == 0x40) return 2;       /* resolvable private (RPA) */
    if (top == 0x00) return 3;       /* non-resolvable private (NRPA) */
    return 1;
}

static uint8_t classify_phy(uint8_t prim_phy) {
    if (prim_phy == BLE_HCI_LE_PHY_CODED) return 3;   /* S2/S8 indistinct here */
    return 1;                                          /* 1M (2M not used for adv) */
}

static void fill_from_props(BleAdvEvent *ev, uint8_t props) {
    ev->conn = (props & BLE_HCI_ADV_CONN_MASK) ? 1 : 0;
    if (props & BLE_HCI_ADV_SCAN_RSP_MASK)        ev->adv_type = 3;
    else if (props & BLE_HCI_ADV_DIRECT_MASK)     ev->adv_type = 1;
    else if (props & BLE_HCI_ADV_CONN_MASK)       ev->adv_type = 0;
    else if (props & BLE_HCI_ADV_SCAN_MASK)       ev->adv_type = 4;
    else                                          ev->adv_type = 2;
    if (!(props & BLE_HCI_ADV_LEGACY_MASK))       ev->adv_type = 5;
}

static void parse_adv_data(BleAdvEvent *ev, const uint8_t *data, uint8_t len) {
    struct ble_hs_adv_fields f;
    if (ble_hs_adv_parse_fields(&f, data, len) != 0) return;

    ev->flags = f.flags;

    if (f.name != NULL && f.name_len > 0) {
        uint8_t copy = f.name_len;
        if (copy > sizeof(ev->name)) copy = sizeof(ev->name);
        memcpy(ev->name, f.name, copy);
        ev->name_len = f.name_len;        /* full length (may exceed 16 → $BX) */
    }

    if (f.mfg_data != NULL && f.mfg_data_len >= 2) {
        ev->company_id = (int32_t)(f.mfg_data[0] | (f.mfg_data[1] << 8));
        uint8_t copy = f.mfg_data_len;
        if (copy > sizeof(ev->manuf)) copy = sizeof(ev->manuf);
        memcpy(ev->manuf, f.mfg_data, copy);
        ev->manuf_len = f.mfg_data_len;
        if (f.mfg_data_len > 2) ev->truncated = 1;   /* MSD beyond company id */
    }

    if (f.num_uuids16 > 0) {
        ev->svc_uuid16 = f.uuids16[0].value;
        uint8_t pos = 0;
        for (int i = 0; i < f.num_uuids16 && (size_t)(pos + 2) <= sizeof(ev->svc_list); i++) {
            uint16_t u = f.uuids16[i].value;
            ev->svc_list[pos++] = (uint8_t)(u & 0xFF);
            ev->svc_list[pos++] = (uint8_t)(u >> 8);
        }
        ev->svc_list_len = pos;
        if (f.num_uuids16 > 1) ev->truncated = 1;
    }
    if (f.num_uuids128 > 0) ev->truncated = 1;        /* 128-bit UUIDs → $BX */
    if (ev->name_len > 16) ev->truncated = 1;
}

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    (void)arg;
    if (event->type == BLE_GAP_EVENT_EXT_DISC) {
        struct ble_gap_ext_disc_desc *d = &event->ext_disc;
        BleAdvEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.local_timer_us = (uint64_t)esp_timer_get_time();
        memcpy(ev.bdaddr, d->addr.val, 6);
        ev.addr_type = classify_addr(&d->addr);
        ev.rssi = d->rssi;
        ev.channel = 0;                 /* primary channel not surfaced by host */
        ev.company_id = -1;
        ev.phy = classify_phy(d->prim_phy);
        fill_from_props(&ev, d->props);
        parse_adv_data(&ev, d->data, d->length_data);
        if (g_q) xQueueSend(g_q, &ev, 0);   /* drop-newest on full */
        return 0;
    }
    if (event->type == BLE_GAP_EVENT_DISC) {
        struct ble_gap_disc_desc *d = &event->disc;
        BleAdvEvent ev;
        memset(&ev, 0, sizeof(ev));
        ev.local_timer_us = (uint64_t)esp_timer_get_time();
        memcpy(ev.bdaddr, d->addr.val, 6);
        ev.addr_type = classify_addr(&d->addr);
        ev.rssi = d->rssi;
        ev.channel = 0;
        ev.company_id = -1;
        ev.phy = 1;
        /* legacy event_type → adv_type */
        switch (d->event_type) {
            case BLE_HCI_ADV_RPT_EVTYPE_ADV_IND:     ev.adv_type = 0; ev.conn = 1; break;
            case BLE_HCI_ADV_RPT_EVTYPE_DIR_IND:     ev.adv_type = 1; ev.conn = 1; break;
            case BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND: ev.adv_type = 2; break;
            case BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP:    ev.adv_type = 3; break;
            case BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND:    ev.adv_type = 4; break;
            default:                                  ev.adv_type = 2; break;
        }
        parse_adv_data(&ev, d->data, d->length_data);
        if (g_q) xQueueSend(g_q, &ev, 0);
        return 0;
    }
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        start_scan();   /* duration 0 means this should not fire, but be safe */
        return 0;
    }
    return 0;
}

static void start_scan(void) {
    struct ble_gap_ext_disc_params uncoded = {
        .itvl = 0x0010, .window = 0x0010, .passive = 1,
    };
    struct ble_gap_ext_disc_params coded = uncoded;

    const struct ble_gap_ext_disc_params *p_uncoded =
        (g_phy_mask & 0x01) ? &uncoded : NULL;
    const struct ble_gap_ext_disc_params *p_coded =
        (g_phy_mask & 0x02) ? &coded : NULL;

    int rc = ble_gap_ext_disc(g_own_addr_type,
                              0 /*duration: forever*/, 0 /*period*/,
                              0 /*filter_duplicates*/,
                              BLE_HCI_SCAN_FILT_NO_WL,
                              0 /*limited*/,
                              p_uncoded, p_coded,
                              gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gap_ext_disc rc=%d", rc);
        g_running = false;
        return;
    }
    g_running = true;
}

static void on_sync(void) {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) { ESP_LOGW(TAG, "ensure_addr rc=%d", rc); }
    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) { ESP_LOGW(TAG, "infer_auto rc=%d", rc); g_own_addr_type = 0; }
    start_scan();
}

static void on_reset(int reason) {
    ESP_LOGW(TAG, "nimble reset; reason=%d", reason);
    g_running = false;
}

static void ble_host_task(void *param) {
    (void)param;
    nimble_port_run();              /* returns only on nimble_port_stop() */
    nimble_port_freertos_deinit();
}

void ble_scan_init(uint8_t phy_mask) {
    g_phy_mask = (phy_mask == 0) ? 3 : phy_mask;
    g_q = xQueueCreate(BLE_RING_SIZE, sizeof(BleAdvEvent));

    nimble_port_init();
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    nimble_port_freertos_init(ble_host_task);
}

bool ble_scan_pop(BleAdvEvent *out) {
    if (!g_q || !out) return false;
    return xQueueReceive(g_q, out, 0) == pdTRUE;
}

bool ble_scan_running(void) { return g_running; }
