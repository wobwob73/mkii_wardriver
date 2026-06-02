#include "queues.h"

#include "hardware/sync.h"

#include <string.h>

#if (WIFI_DET_QUEUE_CAP & (WIFI_DET_QUEUE_CAP - 1)) != 0
#error "WIFI_DET_QUEUE_CAP must be a power of two"
#endif
#if (BLE_QUEUE_CAP & (BLE_QUEUE_CAP - 1)) != 0
#error "BLE_QUEUE_CAP must be a power of two"
#endif
#if (BLE_EXT_QUEUE_CAP & (BLE_EXT_QUEUE_CAP - 1)) != 0
#error "BLE_EXT_QUEUE_CAP must be a power of two"
#endif

/* --- WiFi detection rings (one per family) ----------------------------- */
static detection_t       g_wifi_ring[N_WIFI_FAMILIES][WIFI_DET_QUEUE_CAP];
static volatile uint32_t g_wifi_w[N_WIFI_FAMILIES];
static volatile uint32_t g_wifi_r[N_WIFI_FAMILIES];

/* --- BLE event ring ---------------------------------------------------- */
static ble_event_t       g_ble_ring[BLE_QUEUE_CAP];
static volatile uint32_t g_ble_w, g_ble_r;

/* --- BLE extended-payload ring ----------------------------------------- */
static ble_ext_t         g_bext_ring[BLE_EXT_QUEUE_CAP];
static volatile uint32_t g_bext_w, g_bext_r;

static volatile uint32_t g_overflow = 0;

static inline uint32_t wifi_next(uint32_t i) { return (i + 1) & (WIFI_DET_QUEUE_CAP - 1); }
static inline uint32_t ble_next(uint32_t i)  { return (i + 1) & (BLE_QUEUE_CAP - 1); }
static inline uint32_t bext_next(uint32_t i) { return (i + 1) & (BLE_EXT_QUEUE_CAP - 1); }

void queues_init(void) {
    memset(g_wifi_ring, 0, sizeof(g_wifi_ring));
    for (int f = 0; f < N_WIFI_FAMILIES; f++) { g_wifi_w[f] = 0; g_wifi_r[f] = 0; }
    memset(g_ble_ring, 0, sizeof(g_ble_ring));
    g_ble_w = g_ble_r = 0;
    memset(g_bext_ring, 0, sizeof(g_bext_ring));
    g_bext_w = g_bext_r = 0;
    g_overflow = 0;
}

bool wifi_det_q_push(uint8_t fam, const detection_t *e) {
    if (!e || fam >= N_WIFI_FAMILIES) return false;
    uint32_t w = g_wifi_w[fam];
    uint32_t r = g_wifi_r[fam];
    if (wifi_next(w) == r) { g_overflow++; return false; }
    g_wifi_ring[fam][w] = *e;
    __dmb();
    g_wifi_w[fam] = wifi_next(w);
    return true;
}

bool wifi_det_q_pop(uint8_t fam, detection_t *out) {
    if (!out || fam >= N_WIFI_FAMILIES) return false;
    uint32_t w = g_wifi_w[fam];
    uint32_t r = g_wifi_r[fam];
    if (w == r) return false;
    __dmb();
    *out = g_wifi_ring[fam][r];
    g_wifi_r[fam] = wifi_next(r);
    return true;
}

uint16_t wifi_det_q_used(uint8_t fam) {
    if (fam >= N_WIFI_FAMILIES) return 0;
    uint32_t w = g_wifi_w[fam];
    uint32_t r = g_wifi_r[fam];
    return (uint16_t)((w - r) & (WIFI_DET_QUEUE_CAP - 1));
}

bool ble_q_push(const ble_event_t *e) {
    if (!e) return false;
    uint32_t w = g_ble_w, r = g_ble_r;
    if (ble_next(w) == r) { g_overflow++; return false; }
    g_ble_ring[w] = *e;
    __dmb();
    g_ble_w = ble_next(w);
    return true;
}

bool ble_q_pop(ble_event_t *out) {
    if (!out) return false;
    uint32_t w = g_ble_w, r = g_ble_r;
    if (w == r) return false;
    __dmb();
    *out = g_ble_ring[r];
    g_ble_r = ble_next(r);
    return true;
}

uint16_t ble_q_used(void) {
    uint32_t w = g_ble_w, r = g_ble_r;
    return (uint16_t)((w - r) & (BLE_QUEUE_CAP - 1));
}

bool ble_ext_q_push(const ble_ext_t *e) {
    if (!e) return false;
    uint32_t w = g_bext_w, r = g_bext_r;
    if (bext_next(w) == r) { g_overflow++; return false; }
    g_bext_ring[w] = *e;
    __dmb();
    g_bext_w = bext_next(w);
    return true;
}

bool ble_ext_q_pop(ble_ext_t *out) {
    if (!out) return false;
    uint32_t w = g_bext_w, r = g_bext_r;
    if (w == r) return false;
    __dmb();
    *out = g_bext_ring[r];
    g_bext_r = bext_next(r);
    return true;
}

uint32_t queues_overflow_count(void) { return g_overflow; }
