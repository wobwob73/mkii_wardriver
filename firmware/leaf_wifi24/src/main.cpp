#include "leaf_defs.h"
#include "uart_proto.h"
#include "config.h"
#include "heartbeat.h"
#include "wifi_scan.h"
#include "wids_monitor.h"
#include "cmd_handler.h"

#include <Arduino.h>
#include <esp_system.h>

static uart_proto::LineReceiver g_rx;
static uint32_t g_boot_ms = 0;
static bool g_defaults_applied_for_timeout = false;

void setup() {
    Serial.begin(230400);
    g_boot_ms = millis();

    cfg::init();
    hb::init();
    wifi_scan::init();
    wids_monitor::init();
    cmd_handler::init();

    delay(20);
    hb::send_immediate();
}

static void service_uart_rx() {
    while (Serial.available()) {
        int b = Serial.read();
        if (b < 0) break;
        g_rx.feed((uint8_t)b);
        if (g_rx.has_line()) {
            char copy[MAX_LINE_LEN + 1];
            size_t n = g_rx.length();
            if (n > MAX_LINE_LEN) n = MAX_LINE_LEN;
            memcpy(copy, g_rx.line(), n);
            copy[n] = '\0';
            g_rx.clear();
            cmd_handler::handle_line(copy, n);
        }
    }
}

static void handle_config_timeout() {
    if (cfg::adopted()) return;
    if (g_defaults_applied_for_timeout) return;
    if ((millis() - g_boot_ms) < CONFIG_TIMEOUT_MS) return;

    cfg::apply_defaults();
    cmd_handler::apply_initial_mode();
    g_defaults_applied_for_timeout = true;
    hb::send_immediate();
}

void loop() {
    service_uart_rx();
    handle_config_timeout();

    if (cfg::adopted()) {
        if (cfg::mode() == LEAF_MODE_SCAN) {
            wifi_scan::process();
        } else {
            wids_monitor::process();
        }
    }

    if (hb::due()) {
        hb::send();
        hb::watchdog_heap();
    }
}
