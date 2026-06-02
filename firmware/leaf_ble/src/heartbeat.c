#include "heartbeat.h"
#include "uart_proto.h"
#include "config.h"

#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

#include <stdio.h>

static uint32_t g_scan_count;
static uint32_t g_err_count;
static int64_t  g_last_hb_us;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

void hb_init(void) {
    g_scan_count = 0;
    g_err_count = 0;
    g_last_hb_us = esp_timer_get_time();
}

void hb_note_scan_window(void) { g_scan_count++; }
void hb_note_error(void)       { g_err_count++; }

bool hb_due(void) {
    return (now_ms() - (uint32_t)(g_last_hb_us / 1000)) >= HB_INTERVAL_MS;
}

uint32_t hb_scan_count(void) { return g_scan_count; }

bool hb_send(void) {
    char body[80];
    /* $HB,id,uptime_s,free_heap,scan_count,err_count (blebt §4.5). For BLE-1
     * scan_count is the number of completed 1 s scan windows. */
    snprintf(body, sizeof(body), "$HB,%s,%lu,%lu,%lu,%lu",
             cfg_id_str(),
             (unsigned long)(esp_timer_get_time() / 1000000),
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)g_scan_count,
             (unsigned long)g_err_count);
    g_last_hb_us = esp_timer_get_time();
    return uart_proto_send_framed(body);
}

bool hb_send_immediate(void) { return hb_send(); }
