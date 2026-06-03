#include "branch_uart.h"
#include "pal.h"

#include <string.h>

static line_receiver_t g_lr[N_BRANCH_UARTS];
static uint32_t        g_ok[N_BRANCH_UARTS];
static uint32_t        g_bad[N_BRANCH_UARTS];
static branch_line_cb_t g_cb;
static void *          g_ctx;

void branch_uart_init(void) {
    for (uint8_t b = 0; b < N_BRANCH_UARTS; b++) {
        proto_lr_init(&g_lr[b]);
        g_ok[b] = 0;
        g_bad[b] = 0;
    }
    g_cb = NULL;
    g_ctx = NULL;
}

void branch_uart_register_cb(branch_line_cb_t cb, void *ctx) {
    g_cb = cb;
    g_ctx = ctx;
}

void branch_uart_pump(void) {
    for (uint8_t b = 0; b < N_BRANCH_UARTS; b++) {
        uint8_t chunk[128];
        while (1) {
            int n = pal_branch_rx_read(b, chunk, sizeof(chunk));
            if (n <= 0) break;
            for (int i = 0; i < n; i++) {
                proto_lr_feed(&g_lr[b], chunk[i]);
                if (proto_lr_has_line(&g_lr[b])) {
                    size_t llen = g_lr[b].len;
                    char copy[MAX_LINE_LEN + 1];
                    if (llen > MAX_LINE_LEN) llen = MAX_LINE_LEN;
                    memcpy(copy, g_lr[b].buf, llen);
                    copy[llen] = '\0';
                    proto_lr_clear(&g_lr[b]);

                    if (!proto_validate_line(copy, llen)) {
                        g_bad[b]++;
                        continue;
                    }
                    g_ok[b]++;
                    if (g_cb) g_cb(b, copy, llen, g_ctx);
                }
            }
            if (n < (int)sizeof(chunk)) break;
        }
    }
}

uint32_t branch_uart_lines_ok(uint8_t branch_idx) {
    if (branch_idx >= N_BRANCH_UARTS) return 0;
    return g_ok[branch_idx];
}

uint32_t branch_uart_lines_bad(uint8_t branch_idx) {
    if (branch_idx >= N_BRANCH_UARTS) return 0;
    return g_bad[branch_idx];
}
