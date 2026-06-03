#include "heartbeat.h"

#include "uart_proto.h"
#include "config.h"

#include <esp_system.h>
#include <esp_heap_caps.h>
#include <stdio.h>

namespace hb {

static uint32_t g_scan_count = 0;
static uint32_t g_err_count = 0;
static uint32_t g_last_hb_ms = 0;
static uint8_t  g_low_heap_strikes = 0;

void init() {
    g_scan_count = 0;
    g_err_count = 0;
    g_last_hb_ms = millis();
    g_low_heap_strikes = 0;
}

void note_scan_complete() { g_scan_count++; }
void note_error()         { g_err_count++; }
void note_command_processed() {
    send_immediate();
}

bool due() {
    return (millis() - g_last_hb_ms) >= HB_INTERVAL_MS;
}

uint32_t uptime_s()   { return millis() / 1000; }
uint32_t free_heap()  { return esp_get_free_heap_size(); }
uint32_t scan_count() { return g_scan_count; }
uint32_t err_count()  { return g_err_count; }

bool send() {
    char body[80];
    snprintf(body, sizeof(body), "$HB,%s,%lu,%lu,%lu,%lu",
             cfg::id_str(),
             (unsigned long)uptime_s(),
             (unsigned long)free_heap(),
             (unsigned long)g_scan_count,
             (unsigned long)g_err_count);
    g_last_hb_ms = millis();
    return uart_proto::send_framed(body);
}

bool send_immediate() {
    return send();
}

void watchdog_heap() {
    uint32_t free = free_heap();
    if (free >= HEAP_MIN_BYTES) {
        g_low_heap_strikes = 0;
        return;
    }
    g_low_heap_strikes++;
    if (g_low_heap_strikes >= 3) {
        esp_restart();
    }
}

}
