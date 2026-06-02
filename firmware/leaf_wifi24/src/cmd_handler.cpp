#include "cmd_handler.h"

#include "leaf_defs.h"
#include "uart_proto.h"
#include "config.h"
#include "heartbeat.h"
#include "wifi_scan.h"
#include "wids_monitor.h"

#include <esp_system.h>
#include <string.h>
#include <stdlib.h>

namespace cmd_handler {

static bool g_initial_applied = false;

void init() {
    g_initial_applied = false;
}

static const char *prefix(const char *line) {
    return line + 1;
}

static int parse_int(const char *s, int dflt) {
    if (!s || !*s) return dflt;
    return (int)strtol(s, nullptr, 10);
}

static void switch_to_mode(LeafMode m) {
    if (m == LEAF_MODE_SCAN) {
        wids_monitor::stop();
    } else {
        wids_monitor::start();
    }
}

void apply_initial_mode() {
    switch_to_mode(cfg::mode());
    g_initial_applied = true;
}

static void handle_cf(char **f, int n) {
    if (n < 6) { hb::note_error(); return; }
    const char *leaf_id = f[1];
    LeafMode mode = (LeafMode)parse_int(f[2], 0);
    uint8_t channel = (uint8_t)parse_int(f[3], 1);
    uint16_t param1 = (uint16_t)parse_int(f[4], 0);
    uint16_t param2 = (uint16_t)parse_int(f[5], 0);

    if (!cfg::adopt_from_cf(leaf_id, mode, channel, param1, param2)) {
        hb::note_error();
        return;
    }
    switch_to_mode(mode);
    g_initial_applied = true;
    hb::send_immediate();
}

static void handle_ch(char **f, int n) {
    if (n < 3) { hb::note_error(); return; }
    const char *leaf_id = f[1];
    if (!valid_leaf_id(leaf_id)) { hb::note_error(); return; }
    if (strcmp(leaf_id, cfg::id_str()) != 0) {
        return;
    }
    if (cfg::mode() != LEAF_MODE_WIDS) {
        hb::send_immediate();
        return;
    }
    uint16_t mask = (uint16_t)parse_int(f[2], 0);
    if (!cfg::set_channel_mask(mask)) {
        hb::send_immediate();
        return;
    }
    wids_monitor::apply_channel_mask(cfg::channel_mask());
    hb::send_immediate();
}

static void handle_pg(char **f, int n) {
    if (n < 2) { hb::note_error(); return; }
    if (!valid_leaf_id(f[1])) return;
    if (strcmp(f[1], cfg::id_str()) != 0) return;
    hb::send_immediate();
}

static void handle_rb(char **f, int n) {
    if (n < 2) { hb::note_error(); return; }
    if (!valid_leaf_id(f[1])) return;
    if (strcmp(f[1], cfg::id_str()) != 0) return;
    delay(50);
    esp_restart();
}

void handle_line(char *line, size_t len) {
    if (!line || len < 5) { hb::note_error(); return; }
    if (!uart_proto::validate_checksum(line, len)) {
        hb::note_error();
        return;
    }
    char *star = nullptr;
    for (size_t i = 1; i < len; i++) {
        if (line[i] == '*') { star = line + i; break; }
    }
    if (!star) { hb::note_error(); return; }
    *star = '\0';

    char *fields[16];
    int n = uart_proto::split_fields(line + 1, fields, 16);
    if (n < 1) { hb::note_error(); return; }

    if (strcmp(fields[0], "CF") == 0) {
        handle_cf(fields, n);
    } else if (strcmp(fields[0], "CH") == 0) {
        handle_ch(fields, n);
    } else if (strcmp(fields[0], "PG") == 0) {
        handle_pg(fields, n);
    } else if (strcmp(fields[0], "RB") == 0) {
        handle_rb(fields, n);
    } else {
        hb::note_error();
    }
}

}
