#include "usb_cdc_app.h"
#include "pal.h"

#include <string.h>

static line_receiver_t g_lr;
static usb_line_cb_t   g_cb;
static void *          g_ctx;
static uint32_t        g_in;
static uint32_t        g_out;

void usb_cdc_app_init(void) {
    proto_lr_init(&g_lr);
    g_cb = NULL;
    g_ctx = NULL;
    g_in = 0;
    g_out = 0;
}

void usb_cdc_app_register_cb(usb_line_cb_t cb, void *ctx) {
    g_cb = cb;
    g_ctx = ctx;
}

void usb_cdc_app_pump(void) {
    uint8_t chunk[128];
    while (1) {
        int n = pal_usb_cdc_read(chunk, sizeof(chunk));
        if (n <= 0) break;
        for (int i = 0; i < n; i++) {
            proto_lr_feed(&g_lr, chunk[i]);
            if (proto_lr_has_line(&g_lr)) {
                size_t llen = g_lr.len;
                char copy[MAX_LINE_LEN + 1];
                if (llen > MAX_LINE_LEN) llen = MAX_LINE_LEN;
                memcpy(copy, g_lr.buf, llen);
                copy[llen] = '\0';
                proto_lr_clear(&g_lr);
                if (proto_validate_line(copy, llen)) {
                    g_in++;
                    if (g_cb) g_cb(copy, llen, g_ctx);
                }
            }
        }
        if (n < (int)sizeof(chunk)) break;
    }
}

void usb_cdc_app_send_line(const char *line, size_t len) {
    if (!line || len == 0) return;
    if (!pal_usb_cdc_attached()) return;
    pal_usb_cdc_write((const uint8_t *)line, len);
    g_out++;
}

uint32_t usb_cdc_app_lines_in(void) { return g_in; }
uint32_t usb_cdc_app_lines_out(void) { return g_out; }
