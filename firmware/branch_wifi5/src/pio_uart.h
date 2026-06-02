#pragma once

#include "branch_defs.h"
#include "hardware/pio.h"

typedef struct {
    PIO     pio;
    uint    sm;
    uint    rx_pin;
    int     dma_chan;
    uint32_t *ring;
    size_t  ring_words;
    volatile uint32_t read_pos;
} pio_uart_rx_t;

bool pio_uart_subsys_init(void);

bool pio_uart_rx_init(pio_uart_rx_t *u, uint pin, uint sm, uint32_t *ring, size_t ring_words);

int pio_uart_rx_read(pio_uart_rx_t *u, uint8_t *out, size_t cap);

void pio_uart_tx_send(uint pin, const uint8_t *bytes, size_t n);

void pio_uart_tx_send_line(uint pin, const char *line);

/* Cumulative count of PIO TX retarget self-check failures. A non-zero value
 * means the SM's PINCTRL did not end up pointing at the requested pin and
 * the offending send was dropped. Surface in $BS for field debugging. */
uint32_t pio_uart_tx_retarget_error_count(void);
