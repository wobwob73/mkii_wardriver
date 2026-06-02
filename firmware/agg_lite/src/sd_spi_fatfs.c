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
 *   fatfs           — the real FatFs-over-SPI backend over the vendored carlk3
 *                     library (third_party/pico_fatfs_spi/, §9). Selected with
 *                     -DAGG_SD_BACKEND=fatfs (see CMakeLists.txt). Builds + links
 *                     in CI; writing a card is bench-verified (§13.3).
 *
 * Everything above this file (sd_log.c, the ring, session management, the $LA
 * sd_ok/sd_kb fields) is identical regardless of which backend is linked.
 */

#include "sd_spi_fatfs.h"

#if defined(AGG_SD_BACKEND_FATFS)

/* ---------------------------------------------------------------------------
 * Real FatFs-over-SPI backend — a thin adapter over the vendored carlk3
 * no-OS-FatFS-SD-SPI-RPi-Pico library (third_party/pico_fatfs_spi/, Apache-2.0,
 * pinned commit 196016f). The SPI0 bus + card wiring is supplied by the app in
 * sd_hw_config.c; this file maps the sd_spi_fatfs.h interface onto FatFs.
 *
 * NOTE: builds + links in CI, but writing an actual card is HARDWARE-
 * VERIFICATION-PENDING (lite_aggregator_v1_0.md §13.3 bench step).
 * ------------------------------------------------------------------------- */
#include "ff.h"
#include "sd_card.h"
#include "hw_config.h"

static sd_card_t *g_sd  = NULL;
static FIL        g_fil;
static bool       g_mounted = false;
static bool       g_open    = false;

bool sd_be_init(void) {
    g_mounted = false;
    g_open = false;
    /* Wires up the driver (init/read/write pointers, CS + card-detect GPIO).
     * Idempotent; also invoked by the diskio glue on first mount. */
    sd_init_driver();
    g_sd = sd_get_by_num(0);
    return g_sd != NULL;
}

bool sd_be_present(void) {
    if (!g_sd) return false;
    return sd_card_detect(g_sd);     /* reads card-detect GP22 */
}

bool sd_be_mount(void) {
    if (!g_sd) return false;
    FRESULT fr = f_mount(&g_sd->fatfs, g_sd->pcName, 1 /*mount now*/);
    g_mounted = (fr == FR_OK);
    return g_mounted;
}

void sd_be_unmount(void) {
    if (g_open) { f_close(&g_fil); g_open = false; }
    if (g_sd && g_mounted) f_unmount(g_sd->pcName);
    g_mounted = false;
}

bool sd_be_mkdir_p(const char *path) {
    if (!g_mounted || !path) return false;
    FRESULT fr = f_mkdir(path);
    return (fr == FR_OK || fr == FR_EXIST);
}

bool sd_be_rename(const char *from, const char *to) {
    if (!g_mounted || !from || !to) return false;
    return f_rename(from, to) == FR_OK;
}

bool sd_be_open_append(const char *path) {
    if (!g_mounted || !path) return false;
    if (g_open) { f_close(&g_fil); g_open = false; }
    /* Open once, write many; FA_OPEN_APPEND creates-or-opens and seeks to end. */
    FRESULT fr = f_open(&g_fil, path, FA_WRITE | FA_OPEN_APPEND);
    g_open = (fr == FR_OK);
    return g_open;
}

int sd_be_write(const uint8_t *data, size_t len) {
    if (!g_mounted || !g_open || !data) return -1;
    UINT bw = 0;
    FRESULT fr = f_write(&g_fil, data, (UINT)len, &bw);
    if (fr != FR_OK) {
        /* FR_DISK_ERR / FR_NOT_READY on hot-remove: sd_log reopens/remounts. */
        return -1;
    }
    return (int)bw;
}

bool sd_be_sync(void) {
    if (!g_mounted || !g_open) return false;
    return f_sync(&g_fil) == FR_OK;
}

void sd_be_close(void) {
    if (g_open) { f_close(&g_fil); g_open = false; }
}

bool sd_be_ok(void) {
    return g_mounted && g_open;
}

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
