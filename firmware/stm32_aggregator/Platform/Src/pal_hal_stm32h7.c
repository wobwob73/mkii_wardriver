/*
 * pal_hal_stm32h7.c — STM32H7 HAL implementation of the MKII PAL.
 *
 * Compiled only by the on-target build (CMake -DMKII_STM32_TARGET=on).
 * The host-smoke build uses Platform/Src/pal_hal.c (stub) so the App/
 * layer can be unit-checked without the HAL.
 */

#include "pal.h"
#include "main.h"
#include "stm32h7xx_hal.h"

#include "ff.h"

#include <string.h>
#include <stdlib.h>

extern UART_HandleTypeDef *mkii_uart_handle(uint8_t b);
extern DMA_HandleTypeDef  *mkii_dma_handle(uint8_t b);
extern int CDC_Transmit_FS(uint8_t *Buf, uint16_t Len);

static pal_pps_cb_t g_pps_cb = NULL;
static FATFS        g_fatfs;
static bool         g_fatfs_mounted = false;

uint64_t pal_time_us_64(void) {
    /*
     * 64-bit microsecond timestamp = (g_tim2_overflow_hi << 32) | TIM2->CNT.
     *
     * The naive hi/lo/hi double-read leaves a small window: the hardware
     * counter wraps to a small value (UIF asserts), but the overflow ISR
     * hasn't yet incremented g_tim2_overflow_hi. In that window hi1 == hi2,
     * but lo belongs to the next 32-bit epoch — the function returns a
     * timestamp ~71.6 minutes in the past.
     *
     * Fix: also sample TIM2->SR's UIF flag. If UIF is asserted while we
     * read AND the counter is in its low half (i.e., we're past the wrap),
     * fold the carry in by hand. The (lo < 0x80000000) gate keeps us from
     * folding a UIF that was set by an ISR that has already run but whose
     * cleared write hasn't propagated — in that case the counter would
     * still be in the high half from the previous epoch.
     */
    while (1) {
        uint32_t hi = g_tim2_overflow_hi;
        uint32_t lo = __HAL_TIM_GET_COUNTER(&htim2);
        bool overflow_pending = __HAL_TIM_GET_FLAG(&htim2, TIM_FLAG_UPDATE) != RESET;
        uint32_t hi2 = g_tim2_overflow_hi;
        if (hi != hi2) {
            /* The ISR fired between our reads — retry against the new hi. */
            continue;
        }
        if (overflow_pending && lo < 0x80000000u) {
            hi += 1;
        }
        return ((uint64_t)hi << 32) | (uint64_t)lo;
    }
}

uint32_t pal_time_ms(void) {
    return HAL_GetTick();
}

void pal_init(void) {
    /* All peripherals initialized in main() before app_main() is called. */
}

int pal_branch_rx_read(uint8_t branch_idx, uint8_t *out, size_t cap) {
    if (branch_idx >= N_BRANCH_UARTS || !out) return 0;
    DMA_HandleTypeDef *hdma = mkii_dma_handle(branch_idx);
    if (!hdma) return 0;

    uint16_t dma_ndtr = (uint16_t)__HAL_DMA_GET_COUNTER(hdma);
    uint16_t dma_head = (uint16_t)(BRANCH_RX_RING_SZ - dma_ndtr);
    uint16_t tail = g_branch_rx_tail[branch_idx];

    if (dma_head == tail) return 0;

    size_t copied = 0;
    while (tail != dma_head && copied < cap) {
        out[copied++] = g_branch_rx_ring[branch_idx][tail];
        tail = (uint16_t)((tail + 1) % BRANCH_RX_RING_SZ);
    }
    g_branch_rx_tail[branch_idx] = tail;
    return (int)copied;
}

int pal_branch_tx_write(uint8_t branch_idx, const uint8_t *bytes, size_t len) {
    if (branch_idx >= N_BRANCH_UARTS || !bytes || len == 0) return 0;
    UART_HandleTypeDef *h = mkii_uart_handle(branch_idx);
    if (!h) return 0;
    if (HAL_UART_Transmit(h, (uint8_t *)bytes, (uint16_t)len, 100) != HAL_OK) {
        return -1;
    }
    return (int)len;
}

int pal_branch_tx_write_str(uint8_t branch_idx, const char *line) {
    if (!line) return 0;
    return pal_branch_tx_write(branch_idx, (const uint8_t *)line, strlen(line));
}

int pal_usb_cdc_write(const uint8_t *bytes, size_t len) {
    if (!bytes || len == 0) return 0;
    if (!g_usb_cdc_attached) return 0;
    uint32_t t0 = HAL_GetTick();
    while (CDC_Transmit_FS((uint8_t *)bytes, (uint16_t)len) != 0) {
        if (HAL_GetTick() - t0 > 5) return 0;
    }
    return (int)len;
}

int pal_usb_cdc_write_str(const char *line) {
    if (!line) return 0;
    return pal_usb_cdc_write((const uint8_t *)line, strlen(line));
}

int pal_usb_cdc_read(uint8_t *out, size_t cap) {
    if (!out || cap == 0) return 0;
    size_t n = 0;
    while (n < cap) {
        uint16_t r = g_usb_cdc_rx_r;
        uint16_t w = g_usb_cdc_rx_w;
        if (r == w) break;
        out[n++] = g_usb_cdc_rx_ring[r];
        g_usb_cdc_rx_r = (uint16_t)((r + 1) % USB_CDC_RX_BUF_SZ);
    }
    return (int)n;
}

bool pal_usb_cdc_attached(void) {
    return g_usb_cdc_attached != 0;
}

int pal_i2c_read_reg(uint8_t i2c_addr, uint8_t reg, uint8_t *out, size_t n) {
    return (HAL_I2C_Mem_Read(&hi2c1, (uint16_t)(i2c_addr << 1), reg,
                             I2C_MEMADD_SIZE_8BIT, out, (uint16_t)n, 100)
            == HAL_OK) ? (int)n : -1;
}

int pal_i2c_write_reg(uint8_t i2c_addr, uint8_t reg, const uint8_t *bytes, size_t n) {
    return (HAL_I2C_Mem_Write(&hi2c1, (uint16_t)(i2c_addr << 1), reg,
                              I2C_MEMADD_SIZE_8BIT, (uint8_t *)bytes,
                              (uint16_t)n, 100) == HAL_OK) ? (int)n : -1;
}

int pal_i2c_read(uint8_t i2c_addr, uint8_t *out, size_t n) {
    return (HAL_I2C_Master_Receive(&hi2c1, (uint16_t)(i2c_addr << 1),
                                   out, (uint16_t)n, 100) == HAL_OK)
                ? (int)n : -1;
}

int pal_i2c_write(uint8_t i2c_addr, const uint8_t *bytes, size_t n) {
    return (HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)(i2c_addr << 1),
                                    (uint8_t *)bytes, (uint16_t)n, 100)
            == HAL_OK) ? (int)n : -1;
}

void pal_pps_register(pal_pps_cb_t cb) {
    g_pps_cb = cb;
}

void pal_pps_dispatch_from_isr(void) {
    if (g_pps_cb) g_pps_cb(pal_time_us_64());
}

bool pal_sd_present(void) {
    return g_fatfs_mounted || (HAL_SD_GetState(&hsd1) == HAL_SD_STATE_READY);
}

int pal_sd_mount(void) {
    if (HAL_SD_Init(&hsd1) != HAL_OK) return -1;
    FRESULT fr = f_mount(&g_fatfs, "0:", 1);
    if (fr != FR_OK) return -2;
    g_fatfs_mounted = true;
    return 0;
}

int pal_sd_open_append(const char *path, void **handle_out) {
    if (!path || !handle_out) return -1;
    FIL *fp = (FIL *)malloc(sizeof(FIL));
    if (!fp) return -2;
    memset(fp, 0, sizeof(*fp));
    FRESULT fr = f_open(fp, path, FA_OPEN_APPEND | FA_WRITE | FA_READ);
    if (fr != FR_OK) {
        free(fp);
        return -3;
    }
    *handle_out = fp;
    return 0;
}

int pal_sd_write(void *handle, const uint8_t *bytes, size_t n) {
    if (!handle || !bytes) return -1;
    UINT bw = 0;
    FRESULT fr = f_write((FIL *)handle, bytes, (UINT)n, &bw);
    if (fr != FR_OK) return -2;
    return (int)bw;
}

int pal_sd_flush(void *handle) {
    if (!handle) return -1;
    return f_sync((FIL *)handle) == FR_OK ? 0 : -2;
}

int pal_sd_close(void *handle) {
    if (!handle) return -1;
    FRESULT fr = f_close((FIL *)handle);
    free(handle);
    return fr == FR_OK ? 0 : -2;
}

int pal_sd_mkdir_p(const char *path) {
    if (!path) return -1;
    char buf[64];
    size_t len = strlen(path);
    if (len >= sizeof(buf)) return -2;
    memcpy(buf, path, len + 1);
    for (size_t i = 1; i < len; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            (void)f_mkdir(buf);
            buf[i] = '/';
        }
    }
    FRESULT fr = f_mkdir(buf);
    return (fr == FR_OK || fr == FR_EXIST) ? 0 : -3;
}

void pal_led_set(pal_led_t led, bool on) {
    GPIO_PinState s = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
    switch (led) {
        case PAL_LED_BOOT_OK:       HAL_GPIO_WritePin(LED_GREEN_Port, LED_GREEN_Pin, s); break;
        case PAL_LED_TIME_DEGRADED: HAL_GPIO_WritePin(LED_YELLOW_Port, LED_YELLOW_Pin, s); break;
        case PAL_LED_FAULT:         HAL_GPIO_WritePin(LED_RED_Port, LED_RED_Pin, s); break;
    }
}

void pal_led_blink_ms(pal_led_t led, uint32_t period_ms) {
    if (period_ms == 0) { pal_led_set(led, true); return; }
    bool on = ((HAL_GetTick() / (period_ms / 2)) & 1) == 0;
    pal_led_set(led, on);
}

bool pal_button_held(void) {
    return HAL_GPIO_ReadPin(BTN_USER_Port, BTN_USER_Pin) == GPIO_PIN_RESET;
}

void pal_iwdg_kick(void) {
    HAL_IWDG_Refresh(&hiwdg1);
}

void pal_reboot(void) {
    NVIC_SystemReset();
}
