/*
 * sd_hw_config.c — application hardware configuration for the vendored
 * no-OS-FatFS-SD-SPI-RPi-Pico library (AGG_SD_BACKEND=fatfs only).
 *
 * The library is hardware-agnostic: it calls back into spi_get_*/sd_get_* to
 * learn the bus + card wiring. That config is application-specific, so it lives
 * HERE (app code) rather than in the vendored tree, which stays unmodified.
 *
 * SPI0 pin map per lite_aggregator_v1_0.md §1:
 *   SCK GP18, MOSI GP19, MISO GP16, CS GP17, card-detect GP22.
 * Pins/polarity are preliminary pending PCB layout + bench bring-up (§13.3);
 * card_detected_true reflects a typical active-low microSD socket.
 */

#include "hw_config.h"   /* vendored: declares spi_t / sd_card_t + the getters */
#include "agg_defs.h"

#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/irq.h"

/* SPI0 bus. The card is clocked up to ~25 MHz (§1); 12 MHz is a conservative
 * default that the driver can negotiate down from during init. */
static spi_t g_spi = {
    .hw_inst     = SD_SPI_INST,        /* spi0 */
    .miso_gpio   = SD_MISO_PIN,        /* GP16 */
    .mosi_gpio   = SD_MOSI_PIN,        /* GP19 */
    .sck_gpio    = SD_SCK_PIN,         /* GP18 */
    .baud_rate   = 12 * 1000 * 1000,
    .DMA_IRQ_num = DMA_IRQ_0,
};

static sd_card_t g_sd = {
    .pcName              = "0:",
    .spi                 = &g_spi,
    .ss_gpio             = SD_CS_PIN,  /* GP17 */
    .use_card_detect     = true,
    .card_detect_gpio    = SD_CD_PIN,  /* GP22, pull-up */
    .card_detected_true  = 0,          /* active-low socket: present pulls low */
};

size_t spi_get_num(void) { return 1; }
spi_t *spi_get_by_num(size_t num) { return (num == 0) ? &g_spi : NULL; }

size_t sd_get_num(void) { return 1; }
sd_card_t *sd_get_by_num(size_t num) { return (num == 0) ? &g_sd : NULL; }
