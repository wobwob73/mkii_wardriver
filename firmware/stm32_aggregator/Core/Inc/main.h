#pragma once

#include "stm32h7xx_hal.h"

#include "stm32_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----- Pin map ----- */

#define LED_GREEN_Pin     GPIO_PIN_0     /* LD1 — boot OK */
#define LED_GREEN_Port    GPIOB
#define LED_YELLOW_Pin    GPIO_PIN_1     /* LD2 — time degraded */
#define LED_YELLOW_Port   GPIOE
#define LED_RED_Pin       GPIO_PIN_14    /* LD3 — fault */
#define LED_RED_Port      GPIOB
#define BTN_USER_Pin      GPIO_PIN_13    /* B1 BLUE button, active-low */
#define BTN_USER_Port     GPIOC

#define PPS_INPUT_Pin     GPIO_PIN_10    /* PG10 EXTI10 */
#define PPS_INPUT_Port    GPIOG

#define I2C1_SCL_Pin      GPIO_PIN_6
#define I2C1_SCL_Port     GPIOB
#define I2C1_SDA_Pin      GPIO_PIN_7
#define I2C1_SDA_Port     GPIOB

#define USART1_TX_Pin     GPIO_PIN_9     /* PA9 */
#define USART1_TX_Port    GPIOA
#define USART1_RX_Pin     GPIO_PIN_10    /* PA10 */
#define USART1_RX_Port    GPIOA

#define USART2_TX_Pin     GPIO_PIN_5     /* PD5 */
#define USART2_TX_Port    GPIOD
#define USART2_RX_Pin     GPIO_PIN_6     /* PD6 */
#define USART2_RX_Port    GPIOD

#define USART3_TX_Pin     GPIO_PIN_8     /* PD8 — ST-LINK VCP TX */
#define USART3_TX_Port    GPIOD
#define USART3_RX_Pin     GPIO_PIN_9     /* PD9 — ST-LINK VCP RX */
#define USART3_RX_Port    GPIOD

#define UART4_TX_Pin      GPIO_PIN_9     /* PB9 */
#define UART4_TX_Port     GPIOB
#define UART4_RX_Pin      GPIO_PIN_8     /* PB8 */
#define UART4_RX_Port     GPIOB

#define UART5_TX_Pin      GPIO_PIN_13    /* PB13 */
#define UART5_TX_Port     GPIOB
#define UART5_RX_Pin      GPIO_PIN_12    /* PB12 */
#define UART5_RX_Port     GPIOB

#define USART6_TX_Pin     GPIO_PIN_6     /* PC6 */
#define USART6_TX_Port    GPIOC
#define USART6_RX_Pin     GPIO_PIN_7     /* PC7 */
#define USART6_RX_Port    GPIOC

#define SDMMC1_CK_Pin     GPIO_PIN_12    /* PC12 */
#define SDMMC1_CK_Port    GPIOC
#define SDMMC1_CMD_Pin    GPIO_PIN_2     /* PD2 */
#define SDMMC1_CMD_Port   GPIOD
#define SDMMC1_D0_Pin     GPIO_PIN_8     /* PC8 */
#define SDMMC1_D0_Port    GPIOC
#define SDMMC1_D1_Pin     GPIO_PIN_9     /* PC9 */
#define SDMMC1_D1_Port    GPIOC
#define SDMMC1_D2_Pin     GPIO_PIN_10    /* PC10 */
#define SDMMC1_D2_Port    GPIOC
#define SDMMC1_D3_Pin     GPIO_PIN_11    /* PC11 */
#define SDMMC1_D3_Port    GPIOC

#define USB_DM_Pin        GPIO_PIN_11    /* PA11 */
#define USB_DM_Port       GPIOA
#define USB_DP_Pin        GPIO_PIN_12    /* PA12 */
#define USB_DP_Port       GPIOA

/* ----- HAL handle externs ----- */

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;
extern UART_HandleTypeDef huart4;
extern UART_HandleTypeDef huart5;
extern UART_HandleTypeDef huart6;
extern UART_HandleTypeDef huart7;

extern I2C_HandleTypeDef  hi2c1;
extern SD_HandleTypeDef   hsd1;
extern TIM_HandleTypeDef  htim2;
extern IWDG_HandleTypeDef hiwdg1;
extern PCD_HandleTypeDef  hpcd_USB_OTG_FS;

extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart2_rx;
extern DMA_HandleTypeDef hdma_uart4_rx;
extern DMA_HandleTypeDef hdma_uart5_rx;
extern DMA_HandleTypeDef hdma_usart6_rx;

/* ----- Branch RX ring buffers (Core/Src/main.c definitions) ----- */

extern uint8_t  g_branch_rx_ring[N_BRANCH_UARTS][BRANCH_RX_RING_SZ];
extern uint16_t g_branch_rx_tail[N_BRANCH_UARTS];

/* ----- TIM2 overflow counter for 64-bit microsecond timer ----- */

extern volatile uint32_t g_tim2_overflow_hi;

/* ----- USB CDC RX state (set by usbd_cdc_if.c) ----- */

#define USB_CDC_RX_BUF_SZ 4096
extern uint8_t  g_usb_cdc_rx_ring[USB_CDC_RX_BUF_SZ];
extern volatile uint16_t g_usb_cdc_rx_w;
extern volatile uint16_t g_usb_cdc_rx_r;
extern volatile uint8_t  g_usb_cdc_attached;

void Error_Handler(void);
void SystemClock_Config(void);

#ifdef __cplusplus
}
#endif
