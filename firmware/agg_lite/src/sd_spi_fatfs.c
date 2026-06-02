/*
 * sd_spi_fatfs.c — SD backend.
 *
 * Two implementations select at compile time on AGG_SD_BACKEND:
 *
 *   stub  (DEFAULT) — a PLACEHOLDER. It brings up SPI0 + card-detect so the
 *                     wiring is real, then accounts writes (byte counter) but
 *                     does NOT persist them. mount/mkdir/rename/sync succeed so
 *                     the full session + ring state machine runs and the CDC
 *                     mirror is exercised. It is explicitly NOT a durable log.
 *
 *   fatfs           — the real FatFs-over-SPI backend. Compiled only when the
 *                     vendored library is present in third_party/pico_fatfs_spi
 *                     and -DAGG_SD_BACKEND=fatfs is set (see CMakeLists.txt).
 *                     Wiring this is lite_aggregator_v1_0.md §14 open item 5.
 *
 * Everything above this file (sd_log.c, the ring, session management, the $LA
 * sd_ok/sd_kb fields) is identical regardless of which backend is linked.
 */

#include "sd_spi_fatfs.h"

#if defined(AGG_SD_BACKEND_FATFS)

/* ---------------------------------------------------------------------------
 * Real FatFs-over-SPI backend.
 *
 * Intentionally a thin adapter over the vendored library's API. The exact
 * function names depend on the chosen library (carlk3 no-OS-FatFS exposes
 * f_mount/f_open/f_write/f_sync from ff.h plus a board-config hook). Because
 * the library is not vendored in this commit, this branch is guarded so a
 * misconfigured build fails loudly rather than silently linking the stub.
 * ------------------------------------------------------------------------- */
#error "AGG_SD_BACKEND=fatfs selected but third_party/pico_fatfs_spi is not vendored. \
See lite_aggregator_v1_0.md §14 item 5 and firmware/agg_lite/README.md."

#else  /* placeholder backend */

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

static bool     g_mounted = false;
static bool     g_open = false;
static uint64_t g_bytes = 0;

bool sd_be_init(void) {
    /* Bring up SPI0 + CS + card-detect so the physical bus matches §1, even
     * though the placeholder does not issue real card commands. */
    spi_init(SD_SPI_INST, 400 * 1000);   /* slow during init, as a real driver would */
    gpio_set_function(SD_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SD_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(SD_MISO_PIN, GPIO_FUNC_SPI);

    gpio_init(SD_CS_PIN);
    gpio_set_dir(SD_CS_PIN, GPIO_OUT);
    gpio_put(SD_CS_PIN, 1);

    gpio_init(SD_CD_PIN);
    gpio_set_dir(SD_CD_PIN, GPIO_IN);
    gpio_pull_up(SD_CD_PIN);
    return true;
}

/* The placeholder reports a card so the Standalone/Connected logic and the
 * session state machine are exercised. A real backend returns the card-detect
 * line state (active-low CD). */
bool sd_be_present(void) { return true; }

bool sd_be_mount(void)   { g_mounted = true; g_bytes = 0; return true; }
void sd_be_unmount(void) { g_mounted = false; g_open = false; }

bool sd_be_mkdir_p(const char *path) { (void)path; return g_mounted; }
bool sd_be_rename(const char *from, const char *to) {
    (void)from; (void)to; return g_mounted;
}

bool sd_be_open_append(const char *path) {
    (void)path;
    if (!g_mounted) return false;
    g_open = true;
    return true;
}

int sd_be_write(const uint8_t *data, size_t len) {
    (void)data;
    if (!g_mounted || !g_open) return -1;
    g_bytes += len;     /* accounted but discarded — placeholder, not durable */
    return (int)len;
}

bool sd_be_sync(void) { return g_mounted && g_open; }
void sd_be_close(void) { g_open = false; }

bool sd_be_ok(void) { return g_mounted && g_open; }

#endif /* backend select */
