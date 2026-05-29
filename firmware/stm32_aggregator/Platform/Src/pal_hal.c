/*
 * pal_hal.c — Platform Abstraction Layer implementation backed by the
 * STM32H7 HAL. This file is the seam between the MKII App/ business logic
 * and the CubeMX-generated HAL scaffolding (clocks, MspInit, peripheral
 * initialization). It is intentionally thin.
 *
 * To bring up against your CubeMX project:
 *
 *   1. Run CubeMX with the .ioc settings in `README.md §HAL bring-up`.
 *      That generates Core/Inc/main.h, Core/Src/main.c, Core/Src/usart.c,
 *      Core/Src/i2c.c, Core/Src/sdmmc.c, Core/Src/usb_otg.c, etc., plus
 *      Drivers/STM32H7xx_HAL_Driver/.
 *   2. Replace the CubeMX-generated USER CODE sections in main.c with
 *      a call to app_main() (see Core/Src/main_stub.c here for the
 *      exact shape).
 *   3. Implement the function bodies below by calling the HAL handles
 *      that CubeMX produced (huart1, huart2, ..., hi2c1, hsd1, hUsbDeviceFS).
 *      The function signatures here are the contract App/ depends on;
 *      filling them in is a mechanical CubeMX-to-PAL mapping.
 *
 * Everything in this file is a stub returning conservative defaults
 * until you wire it to your CubeMX handles. The file compiles without
 * a CubeMX checkout so the App/ layer can be reviewed before the HAL
 * is in place.
 */

#include "pal.h"

#include <string.h>

void pal_init(void) {
    /* TODO: HAL_Init() + SystemClock_Config() are called from CubeMX
     * main() before app_main(). All MX_*_Init() peripheral inits should
     * be called there too. This pal_init() is for any one-time setup
     * not covered by CubeMX, e.g. priming DMA RX rings.
     */
}

uint64_t pal_time_us_64(void) {
    /* TODO: combine TIM2 (32-bit, 1 MHz) with a 32-bit overflow counter
     * incremented in the TIM2 update ISR to form a continuous 64-bit
     * microsecond timestamp.
     */
    return 0;
}

uint32_t pal_time_ms(void) {
    /* TODO: return HAL_GetTick(). */
    return 0;
}

int pal_branch_rx_read(uint8_t branch_idx, uint8_t *out, size_t cap) {
    /* TODO: snapshot the per-Branch DMA ring's NDTR for the matching USART,
     * compare against the software tail, copy up to `cap` bytes out,
     * advance the tail.
     */
    (void)branch_idx;
    (void)out;
    (void)cap;
    return 0;
}

int pal_branch_tx_write(uint8_t branch_idx, const uint8_t *bytes, size_t len) {
    /* TODO: HAL_UART_Transmit(&huartN, bytes, len, 100). */
    (void)branch_idx;
    (void)bytes;
    return (int)len;
}

int pal_branch_tx_write_str(uint8_t branch_idx, const char *line) {
    if (!line) return 0;
    size_t n = strlen(line);
    return pal_branch_tx_write(branch_idx, (const uint8_t *)line, n);
}

int pal_usb_cdc_write(const uint8_t *bytes, size_t len) {
    /* TODO: CDC_Transmit_FS(bytes, len) wrapped in a small ring buffer to
     * smooth out the FS endpoint's 64-byte packet boundary.
     */
    (void)bytes;
    return (int)len;
}

int pal_usb_cdc_write_str(const char *line) {
    if (!line) return 0;
    return pal_usb_cdc_write((const uint8_t *)line, strlen(line));
}

int pal_usb_cdc_read(uint8_t *out, size_t cap) {
    /* TODO: drain the CDC RX ring filled by CDC_Receive_FS into out[]. */
    (void)out;
    (void)cap;
    return 0;
}

bool pal_usb_cdc_attached(void) {
    /* TODO: track HAL_PCD_ResumeCallback / HAL_PCD_SuspendCallback edges
     * and report whether a host is currently enumerated.
     */
    return false;
}

int pal_i2c_read_reg(uint8_t i2c_addr, uint8_t reg, uint8_t *out, size_t n) {
    /* TODO: HAL_I2C_Mem_Read(&hi2c1, i2c_addr << 1, reg, 1, out, n, 100). */
    (void)i2c_addr; (void)reg; (void)out; (void)n;
    return -1;
}

int pal_i2c_write_reg(uint8_t i2c_addr, uint8_t reg, const uint8_t *bytes, size_t n) {
    /* TODO: HAL_I2C_Mem_Write(&hi2c1, i2c_addr << 1, reg, 1, bytes, n, 100). */
    (void)i2c_addr; (void)reg; (void)bytes; (void)n;
    return -1;
}

int pal_i2c_read(uint8_t i2c_addr, uint8_t *out, size_t n) {
    /* TODO: HAL_I2C_Master_Receive(&hi2c1, i2c_addr << 1, out, n, 100). */
    (void)i2c_addr; (void)out; (void)n;
    return -1;
}

int pal_i2c_write(uint8_t i2c_addr, const uint8_t *bytes, size_t n) {
    /* TODO: HAL_I2C_Master_Transmit(&hi2c1, i2c_addr << 1, bytes, n, 100). */
    (void)i2c_addr; (void)bytes; (void)n;
    return -1;
}

static pal_pps_cb_t g_pps_cb = NULL;

void pal_pps_register(pal_pps_cb_t cb) {
    g_pps_cb = cb;
    /* TODO: enable EXTI on PG10 rising edge if not already enabled by CubeMX. */
}

void pal_pps_dispatch_from_isr(void) {
    /* Hook called from HAL_GPIO_EXTI_Callback in Core/Src/main.c when GPIO_PIN_10 fires. */
    if (g_pps_cb) g_pps_cb(pal_time_us_64());
}

bool pal_sd_present(void) {
    /* TODO: read card-detect GPIO. Return true if SD inserted. */
    return false;
}

int pal_sd_mount(void) {
    /* TODO: f_mount(&fs, "0:", 1). Return 0 on success. */
    return -1;
}

int pal_sd_open_append(const char *path, void **handle_out) {
    /* TODO: FATFS open in append mode, store FIL* in *handle_out. */
    (void)path; (void)handle_out;
    return -1;
}

int pal_sd_write(void *handle, const uint8_t *bytes, size_t n) {
    /* TODO: f_write(handle, bytes, n, &written). */
    (void)handle; (void)bytes; (void)n;
    return -1;
}

int pal_sd_flush(void *handle) {
    /* TODO: f_sync(handle). */
    (void)handle;
    return -1;
}

int pal_sd_close(void *handle) {
    /* TODO: f_close(handle). */
    (void)handle;
    return -1;
}

int pal_sd_mkdir_p(const char *path) {
    /* TODO: walk path and f_mkdir each component. */
    (void)path;
    return -1;
}

void pal_led_set(pal_led_t led, bool on) {
    /* TODO: GPIO_PinState s = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
     *       switch (led) { case PAL_LED_BOOT_OK: HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, s); break; ... }
     */
    (void)led; (void)on;
}

void pal_led_blink_ms(pal_led_t led, uint32_t period_ms) {
    /* TODO: queue an asynchronous blink at period_ms; drive from the main loop. */
    (void)led; (void)period_ms;
}

bool pal_button_held(void) {
    /* TODO: HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_RESET (B1 is active-low). */
    return false;
}

void pal_iwdg_kick(void) {
    /* TODO: HAL_IWDG_Refresh(&hiwdg). */
}

void pal_reboot(void) {
    /* TODO: NVIC_SystemReset(). */
}
