/*
 * main.c — STM32 aggregator on-target entry point.
 *
 * Hand-coded CubeMX-equivalent: SystemClock_Config + MX_*_Init functions for
 * each peripheral, plus the EXTI dispatch into the PAL. After all peripherals
 * are up, calls into App/app_main.c which contains the MKII business logic.
 */

#include "main.h"
#include "stm32h7xx_hal.h"

#include "app_main.h"
#include "pal.h"

#include <string.h>

UART_HandleTypeDef huart1, huart2, huart3, huart4, huart5, huart6, huart7;
I2C_HandleTypeDef  hi2c1;
SD_HandleTypeDef   hsd1;
TIM_HandleTypeDef  htim2;
IWDG_HandleTypeDef hiwdg1;
PCD_HandleTypeDef  hpcd_USB_OTG_FS;

DMA_HandleTypeDef hdma_usart1_rx;
DMA_HandleTypeDef hdma_usart2_rx;
DMA_HandleTypeDef hdma_uart4_rx;
DMA_HandleTypeDef hdma_uart5_rx;
DMA_HandleTypeDef hdma_usart6_rx;

uint8_t  g_branch_rx_ring[N_BRANCH_UARTS][BRANCH_RX_RING_SZ];
uint16_t g_branch_rx_tail[N_BRANCH_UARTS];

volatile uint32_t g_tim2_overflow_hi = 0;

uint8_t  g_usb_cdc_rx_ring[USB_CDC_RX_BUF_SZ];
volatile uint16_t g_usb_cdc_rx_w = 0;
volatile uint16_t g_usb_cdc_rx_r = 0;
volatile uint8_t  g_usb_cdc_attached = 0;

extern void MX_USB_DEVICE_Init(void);
extern void pal_pps_dispatch_from_isr(void);

/* ----- Clock tree: 480 MHz CPU / 240 MHz AXI / 100 MHz APB ----- */

void SystemClock_Config(void) {
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};
    HAL_StatusTypeDef rc;

    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    while ((PWR->D3CR & PWR_D3CR_VOSRDY) == 0U) { }

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_BYPASS;            /* NUCLEO-H753ZI feeds 8 MHz from ST-LINK MCO */
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 4;
    osc.PLL.PLLN = 240;                       /* 8 / 4 * 240 = 480 MHz */
    osc.PLL.PLLP = 2;
    osc.PLL.PLLQ = 20;
    osc.PLL.PLLR = 2;
    osc.PLL.PLLRGE = RCC_PLL1VCIRANGE_1;
    osc.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    osc.PLL.PLLFRACN = 0;
    rc = HAL_RCC_OscConfig(&osc);
    if (rc != HAL_OK) Error_Handler();

    clk.ClockType = (RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                     RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                     RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1);
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.SYSCLKDivider = RCC_SYSCLK_DIV1;
    clk.AHBCLKDivider = RCC_HCLK_DIV2;        /* 240 MHz AXI */
    clk.APB3CLKDivider = RCC_APB3_DIV2;       /* 120 MHz */
    clk.APB1CLKDivider = RCC_APB1_DIV2;       /* 100 MHz APB1 (PCLK1) */
    clk.APB2CLKDivider = RCC_APB2_DIV2;       /* 100 MHz APB2 */
    clk.APB4CLKDivider = RCC_APB4_DIV2;
    rc = HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4);
    if (rc != HAL_OK) Error_Handler();

    /* USB OTG_FS 48 MHz from HSI48 */
    RCC_PeriphCLKInitTypeDef peri = {0};
    peri.PeriphClockSelection = (RCC_PERIPHCLK_USART1 | RCC_PERIPHCLK_USART2 |
                                 RCC_PERIPHCLK_USART3 | RCC_PERIPHCLK_UART4 |
                                 RCC_PERIPHCLK_UART5 | RCC_PERIPHCLK_USART6 |
                                 RCC_PERIPHCLK_I2C1 | RCC_PERIPHCLK_SDMMC |
                                 RCC_PERIPHCLK_USB);
    peri.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2;
    peri.Usart234578ClockSelection = RCC_USART234578CLKSOURCE_D2PCLK1;
    peri.I2c123ClockSelection = RCC_I2C123CLKSOURCE_D2PCLK1;
    peri.SdmmcClockSelection = RCC_SDMMCCLKSOURCE_PLL;
    peri.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
    rc = HAL_RCCEx_PeriphCLKConfig(&peri);
    if (rc != HAL_OK) Error_Handler();

    __HAL_RCC_HSI48_ENABLE();
    while (__HAL_RCC_GET_FLAG(RCC_FLAG_HSI48RDY) == RESET) { }
}

/* ----- GPIO init: pins not owned by individual peripherals ----- */

static void MX_GPIO_Init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};

    /* LEDs */
    HAL_GPIO_WritePin(LED_GREEN_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_YELLOW_Port, LED_YELLOW_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_RED_Port, LED_RED_Pin, GPIO_PIN_RESET);

    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Pin = LED_GREEN_Pin;    HAL_GPIO_Init(LED_GREEN_Port, &g);
    g.Pin = LED_RED_Pin;      HAL_GPIO_Init(LED_RED_Port, &g);
    g.Pin = LED_YELLOW_Pin;   HAL_GPIO_Init(LED_YELLOW_Port, &g);

    /* B1 BLUE button — active-low */
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    g.Pin = BTN_USER_Pin;
    HAL_GPIO_Init(BTN_USER_Port, &g);

    /* PPS input via EXTI10 */
    g.Mode = GPIO_MODE_IT_RISING;
    g.Pull = GPIO_PULLDOWN;
    g.Pin = PPS_INPUT_Pin;
    HAL_GPIO_Init(PPS_INPUT_Port, &g);

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

/* ----- DMA controller init ----- */

static void MX_DMA_Init(void) {
    __HAL_RCC_DMA1_CLK_ENABLE();

    HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream2_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream2_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);
}

/* ----- USART/UART init helpers ----- */

static void uart_init(UART_HandleTypeDef *h, USART_TypeDef *inst,
                      uint32_t baud) {
    h->Instance = inst;
    h->Init.BaudRate = baud;
    h->Init.WordLength = UART_WORDLENGTH_8B;
    h->Init.StopBits = UART_STOPBITS_1;
    h->Init.Parity = UART_PARITY_NONE;
    h->Init.Mode = UART_MODE_TX_RX;
    h->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    h->Init.OverSampling = UART_OVERSAMPLING_16;
    h->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    h->Init.ClockPrescaler = UART_PRESCALER_DIV1;
    h->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(h) != HAL_OK) Error_Handler();
    if (HAL_UARTEx_SetTxFifoThreshold(h, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) Error_Handler();
    if (HAL_UARTEx_SetRxFifoThreshold(h, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) Error_Handler();
    if (HAL_UARTEx_DisableFifoMode(h) != HAL_OK) Error_Handler();
}

static void MX_USART1_Init(void) { uart_init(&huart1, USART1, BRANCH_UART_BAUD); }
static void MX_USART2_Init(void) { uart_init(&huart2, USART2, BRANCH_UART_BAUD); }
static void MX_USART3_Init(void) { uart_init(&huart3, USART3, 115200); }
static void MX_UART4_Init(void)  { uart_init(&huart4, UART4,  BRANCH_UART_BAUD); }
static void MX_UART5_Init(void)  { uart_init(&huart5, UART5,  BRANCH_UART_BAUD); }
static void MX_USART6_Init(void) { uart_init(&huart6, USART6, BRANCH_UART_BAUD); }
static void MX_UART7_Init(void)  { uart_init(&huart7, UART7,  BRANCH_UART_BAUD); }

/* ----- I2C1 init (400 kHz fast-mode for GPS DDC) ----- */

static void MX_I2C1_Init(void) {
    hi2c1.Instance = I2C1;
    hi2c1.Init.Timing = 0x10C0ECFFu;           /* 400 kHz @ 100 MHz APB1 */
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();
}

/* ----- SDMMC1 init (4-bit wide bus) ----- */

static void MX_SDMMC1_SD_Init(void) {
    hsd1.Instance = SDMMC1;
    hsd1.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    hsd1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd1.Init.BusWide = SDMMC_BUS_WIDE_4B;
    hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd1.Init.ClockDiv = 4;
    /* HAL_SD_Init is deferred to first card-present detection in pal_sd_mount. */
}

/* ----- TIM2: 1 MHz free-running 32-bit, IRQ on overflow for 64-bit µs ----- */

static void MX_TIM2_Init(void) {
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 99;                 /* 100 MHz / 100 = 1 MHz */
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 0xFFFFFFFFu;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim2) != HAL_OK) Error_Handler();

    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK) Error_Handler();

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK) Error_Handler();

    HAL_NVIC_SetPriority(TIM2_IRQn, 7, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    HAL_TIM_Base_Start_IT(&htim2);
}

/* ----- IWDG: 8 s window ----- */

static void MX_IWDG_Init(void) {
    hiwdg1.Instance = IWDG1;
    hiwdg1.Init.Prescaler = IWDG_PRESCALER_256;
    hiwdg1.Init.Reload = 1000;                 /* (1000 * 256) / 32 kHz LSI ≈ 8 s */
    hiwdg1.Init.Window = 4095;
    if (HAL_IWDG_Init(&hiwdg1) != HAL_OK) Error_Handler();
}

/* ----- Start DMA RX in circular mode for each Branch UART ----- */

static UART_HandleTypeDef *uart_for_branch(uint8_t b) {
#if MKII_STM32_UNIT == 1
    switch (b) {
        case BRANCH_IDX_W24:    return &huart1;
        case BRANCH_IDX_W5G:    return &huart2;
        case BRANCH_IDX_BLE:    return &huart4;
        case BRANCH_IDX_DOT154: return &huart5;
        case BRANCH_IDX_SEN:    return &huart6;
    }
#else
    switch (b) {
        case BRANCH_IDX_MTC:    return &huart1;
        case BRANCH_IDX_VHF:    return &huart2;
        case BRANCH_IDX_UHF:    return &huart4;
        case BRANCH_IDX_FPV:    return &huart5;
    }
#endif
    return NULL;
}

void mkii_branch_dma_start(void) {
    for (uint8_t b = 0; b < N_BRANCH_UARTS; b++) {
        UART_HandleTypeDef *h = uart_for_branch(b);
        if (!h) continue;
        memset(g_branch_rx_ring[b], 0, BRANCH_RX_RING_SZ);
        g_branch_rx_tail[b] = 0;
        HAL_UART_Receive_DMA(h, g_branch_rx_ring[b], BRANCH_RX_RING_SZ);
        __HAL_UART_DISABLE_IT(h, UART_IT_PE);
        __HAL_UART_DISABLE_IT(h, UART_IT_ERR);
    }
}

uint8_t mkii_uart_for_branch_idx(uint8_t b) {
    return (uart_for_branch(b) != NULL) ? 1 : 0;
}

UART_HandleTypeDef *mkii_uart_handle(uint8_t b) {
    return uart_for_branch(b);
}

DMA_HandleTypeDef *mkii_dma_handle(uint8_t b) {
#if MKII_STM32_UNIT == 1
    switch (b) {
        case BRANCH_IDX_W24:    return &hdma_usart1_rx;
        case BRANCH_IDX_W5G:    return &hdma_usart2_rx;
        case BRANCH_IDX_BLE:    return &hdma_uart4_rx;
        case BRANCH_IDX_DOT154: return &hdma_uart5_rx;
        case BRANCH_IDX_SEN:    return &hdma_usart6_rx;
    }
#else
    switch (b) {
        case BRANCH_IDX_MTC:    return &hdma_usart1_rx;
        case BRANCH_IDX_VHF:    return &hdma_usart2_rx;
        case BRANCH_IDX_UHF:    return &hdma_uart4_rx;
        case BRANCH_IDX_FPV:    return &hdma_uart5_rx;
    }
#endif
    return NULL;
}

/* ----- EXTI dispatch (PPS edge) ----- */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == PPS_INPUT_Pin) {
        pal_pps_dispatch_from_isr();
    }
}

/* ----- TIM2 overflow (carry the 32-bit timer into 64 bits) ----- */

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM2) {
        g_tim2_overflow_hi++;
    }
}

void Error_Handler(void) {
    __disable_irq();
    HAL_GPIO_WritePin(LED_RED_Port, LED_RED_Pin, GPIO_PIN_SET);
    while (1) { }
}

/* ----- Entry point ----- */

int main(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();

    MX_USART1_Init();
    MX_USART2_Init();
    MX_USART3_Init();
    MX_UART4_Init();
    MX_UART5_Init();
    MX_USART6_Init();
    MX_UART7_Init();

    MX_I2C1_Init();
    MX_SDMMC1_SD_Init();
    MX_TIM2_Init();
    MX_USB_DEVICE_Init();
    MX_IWDG_Init();

    mkii_branch_dma_start();

    app_main();

    while (1) { }
}
