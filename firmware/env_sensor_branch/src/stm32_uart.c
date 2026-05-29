#include "stm32_uart.h"

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

void stm32_uart_init(void) {
    uart_init(STM32_UART_INST, UPSTREAM_BAUD);
    gpio_set_function(STM32_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(STM32_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(STM32_UART_INST, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(STM32_UART_INST, true);
}

void stm32_uart_send_line(const char *line, size_t len) {
    if (!line || len == 0) return;
    uart_write_blocking(STM32_UART_INST, (const uint8_t *)line, len);
}

int stm32_uart_read(uint8_t *out, size_t cap) {
    if (!out || cap == 0) return 0;
    size_t n = 0;
    while (n < cap && uart_is_readable(STM32_UART_INST)) {
        out[n++] = (uint8_t)uart_getc(STM32_UART_INST);
    }
    return (int)n;
}
