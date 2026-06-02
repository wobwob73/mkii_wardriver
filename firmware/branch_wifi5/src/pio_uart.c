#include "pio_uart.h"

#include "uart_rx.pio.h"
#include "uart_tx.pio.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"

#include <limits.h>
#include <string.h>

static int g_rx_offset_pio0 = -1;
static int g_tx_offset_pio1 = -1;
static int g_tx_sm_pio1 = -1;
static uint g_tx_current_pin = UINT_MAX;
static uint32_t g_tx_retarget_errors = 0;

bool pio_uart_subsys_init(void) {
    g_rx_offset_pio0 = pio_add_program(pio0, &uart_rx_program);
    if (g_rx_offset_pio0 < 0) return false;

    g_tx_offset_pio1 = pio_add_program(pio1, &uart_tx_program);
    if (g_tx_offset_pio1 < 0) return false;

    g_tx_sm_pio1 = pio_claim_unused_sm(pio1, true);
    if (g_tx_sm_pio1 < 0) return false;

    uart_tx_program_init(pio1, (uint)g_tx_sm_pio1, (uint)g_tx_offset_pio1,
                         LEAF_W5_1_TX_PIN, LEAF_BAUD);
    g_tx_current_pin = LEAF_W5_1_TX_PIN;
    return true;
}

bool pio_uart_rx_init(pio_uart_rx_t *u, uint pin, uint sm,
                      uint32_t *ring, size_t ring_words) {
    if (!u || !ring) return false;
    if ((ring_words & (ring_words - 1)) != 0) return false;
    memset(u, 0, sizeof(*u));
    u->pio = pio0;
    u->sm = sm;
    u->rx_pin = pin;
    u->ring = ring;
    u->ring_words = ring_words;

    uart_rx_program_init(u->pio, sm, (uint)g_rx_offset_pio0, pin, LEAF_BAUD);

    u->dma_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config((uint)u->dma_chan);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_32);
    channel_config_set_dreq(&cfg, pio_get_dreq(u->pio, sm, false));
    int log2 = 0;
    size_t tmp = ring_words;
    while (tmp > 1) { tmp >>= 1; log2++; }
    channel_config_set_ring(&cfg, true, log2 + 2);

    dma_channel_configure((uint)u->dma_chan, &cfg,
                          u->ring, &u->pio->rxf[sm],
                          0xFFFFFFFFu, true);
    u->read_pos = 0;
    return true;
}

int pio_uart_rx_read(pio_uart_rx_t *u, uint8_t *out, size_t cap) {
    if (!u || !out || cap == 0) return 0;
    uint32_t total = 0xFFFFFFFFu;
    uint32_t remaining = dma_channel_hw_addr((uint)u->dma_chan)->transfer_count;
    uint32_t consumed = total - remaining;
    uint32_t available = consumed - u->read_pos;
    if (available == 0) return 0;
    if (available > cap) available = (uint32_t)cap;

    for (uint32_t i = 0; i < available; i++) {
        uint32_t pos = (u->read_pos + i) & (u->ring_words - 1);
        uint32_t word = u->ring[pos];
        out[i] = (uint8_t)(word & 0xFF);
    }
    u->read_pos += available;
    return (int)available;
}

/*
 * uart_tx_retarget_pin — re-point the single shared TX state machine at a new
 * downstream pin. Earlier revisions only re-pinned the GPIO; the SM's PINCTRL
 * OUT_BASE / SIDESET_BASE were never updated, so every leaf's bytes left on
 * the originally configured pin (W1 / W5_1). This implementation:
 *
 *   1. drains the TX FIFO and waits for the in-flight character to complete
 *      (worst case 10 bits @ LEAF_BAUD ≈ 43 µs at 230400; we use 80 µs);
 *   2. stops the SM;
 *   3. restores the previously assigned pin to SIO/input so it stops driving;
 *   4. claims and pulls-high the new target pin via pio_gpio_init;
 *   5. rebuilds a full pio_sm_config with sm_config_set_out_pins / sm_config_set_sideset_pins
 *      pointing at the new pin and applies it via pio_sm_set_config;
 *   6. forces PC back to the program origin (the SM was halted mid-program);
 *   7. self-checks PINCTRL OUT_BASE / SIDESET_BASE against the requested pin
 *      and reports failure (caller's bytes will not be written).
 *
 * The chosen "one TX SM, retarget per byte block" topology stays — switching
 * to one SM per leaf does not scale to the 8-leaf UHF ISM branch.
 */
static bool uart_tx_retarget_pin(uint pin) {
    if (g_tx_sm_pio1 < 0) return false;
    if (pin == g_tx_current_pin) return true;

    const uint sm = (uint)g_tx_sm_pio1;

    /* 1 + 2: drain FIFO, wait for shifter, stop SM. */
    while (!pio_sm_is_tx_fifo_empty(pio1, sm)) tight_loop_contents();
    busy_wait_us(80);
    pio_sm_set_enabled(pio1, sm, false);

    /* 3: release the previous pin so it stops driving (SIO + input). */
    if (g_tx_current_pin != UINT_MAX) {
        gpio_set_function(g_tx_current_pin, GPIO_FUNC_SIO);
        gpio_set_dir(g_tx_current_pin, GPIO_IN);
    }

    /* 4: claim the new pin for PIO output, idle-high. */
    pio_sm_set_pins_with_mask(pio1, sm, (1u << pin), (1u << pin));
    pio_sm_set_pindirs_with_mask(pio1, sm, (1u << pin), (1u << pin));
    pio_gpio_init(pio1, pin);

    /* 5: rebuild and apply config with new OUT/SIDESET base. */
    pio_sm_config c = uart_tx_program_get_default_config((uint)g_tx_offset_pio1);
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_out_pins(&c, pin, 1);
    sm_config_set_sideset_pins(&c, pin);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    float div = (float)clock_get_hz(clk_sys) / (8.0f * (float)LEAF_BAUD);
    sm_config_set_clkdiv(&c, div);
    pio_sm_set_config(pio1, sm, &c);

    /* 6: jump to program start (we halted the SM mid-program). */
    pio_sm_clear_fifos(pio1, sm);
    pio_sm_exec(pio1, sm, pio_encode_jmp((uint)g_tx_offset_pio1));
    pio_sm_set_enabled(pio1, sm, true);

    /* 7: self-check PINCTRL OUT/SIDESET base against the requested pin. */
    uint32_t pinctrl = pio1->sm[sm].pinctrl;
    uint out_base = (pinctrl & PIO_SM0_PINCTRL_OUT_BASE_BITS)
                    >> PIO_SM0_PINCTRL_OUT_BASE_LSB;
    uint ss_base  = (pinctrl & PIO_SM0_PINCTRL_SIDESET_BASE_BITS)
                    >> PIO_SM0_PINCTRL_SIDESET_BASE_LSB;
    if (out_base != pin || ss_base != pin) {
        g_tx_retarget_errors++;
        return false;
    }
    g_tx_current_pin = pin;
    return true;
}

void pio_uart_tx_send(uint pin, const uint8_t *bytes, size_t n) {
    if (!bytes || n == 0 || g_tx_sm_pio1 < 0) return;
    if (!uart_tx_retarget_pin(pin)) return;
    for (size_t i = 0; i < n; i++) {
        pio_sm_put_blocking(pio1, (uint)g_tx_sm_pio1, (uint32_t)bytes[i]);
    }
}

void pio_uart_tx_send_line(uint pin, const char *line) {
    if (!line) return;
    size_t n = strlen(line);
    pio_uart_tx_send(pin, (const uint8_t *)line, n);
}

uint32_t pio_uart_tx_retarget_error_count(void) {
    return g_tx_retarget_errors;
}
