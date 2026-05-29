#include "dispatch.h"
#include "branch_uart.h"
#include "usb_cdc_app.h"
#include "sd_log.h"

#include <string.h>

static uint32_t g_forwarded = 0;
static uint32_t g_unknown = 0;

static bool is_known_prefix(const char *p) {
    static const char *const known[] = {
        "WA", "WP", "ET", "DF", "BS",
        "BD", "BX", "BC_T",
        "ZA", "ZN",
        "LA",
        "SA",
        "HW", "FA",
        "VA",
        "EN", "SB",
        NULL,
    };
    for (int i = 0; known[i]; i++) {
        if (strcmp(p, known[i]) == 0) return true;
    }
    return false;
}

static void on_branch_line(uint8_t branch_idx, const char *line, size_t len, void *ctx) {
    (void)branch_idx;
    (void)ctx;
    char copy[MAX_LINE_LEN + 1];
    if (len > MAX_LINE_LEN) return;
    memcpy(copy, line, len);
    copy[len] = '\0';

    char prefix[8] = {0};
    size_t i = 1;
    size_t pp = 0;
    while (i < len && copy[i] != ',' && copy[i] != '*' && pp < sizeof(prefix) - 1) {
        prefix[pp++] = copy[i++];
    }
    prefix[pp] = '\0';

    if (!is_known_prefix(prefix)) {
        g_unknown++;
        return;
    }

    char with_nl[MAX_LINE_LEN + 2];
    memcpy(with_nl, line, len);
    if (len < sizeof(with_nl) - 1) {
        with_nl[len]     = '\n';
        with_nl[len + 1] = '\0';
        usb_cdc_app_send_line(with_nl, len + 1);
        sd_log_write_line(with_nl, len + 1);
    }
    g_forwarded++;
}

void dispatch_init(void) {
    g_forwarded = 0;
    g_unknown = 0;
}

void dispatch_install(void) {
    branch_uart_register_cb(on_branch_line, NULL);
}

void dispatch_branch_line(uint8_t branch_idx, const char *line, size_t len) {
    on_branch_line(branch_idx, line, len, NULL);
}

uint32_t dispatch_lines_forwarded(void) { return g_forwarded; }
uint32_t dispatch_unknown_prefix(void)  { return g_unknown; }
