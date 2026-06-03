/*
 * stm32h7xx_hal_msp.c — Per-peripheral MSP (MCU Support Package) init.
 *
 * HAL_*_MspInit functions are called from HAL_*_Init. They wire up the
 * peripheral's GPIO pins (mode + alternate function), enable the
 * peripheral's clock domain, and prime NVIC priority + IRQ enables for
 * the peripheral's IRQ vectors. DMA hookups for the Branch UARTs land
 * in HAL_UART_MspInit.
 */

#include "main.h"
#include "stm32h7xx_hal.h"

/* ----- Top-level HAL MSP ----- */

void HAL_MspInit(void) {
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4);
    HAL_NVIC_SetPriority(MemoryManagement_IRQn, 0, 0);
    HAL_NVIC_SetPriority(BusFault_IRQn, 0, 0);
    HAL_NVIC_SetPriority(UsageFault_IRQn, 0, 0);
    HAL_NVIC_SetPriority(SVCall_IRQn, 0, 0);
    HAL_NVIC_SetPriority(DebugMonitor_IRQn, 0, 0);
    HAL_NVIC_SetPriority(PendSV_IRQn, 15, 0);
    HAL_NVIC_SetPriority(SysTick_IRQn, 15, 0);
}

/* ----- UART MSP: assign DMA stream per BC v1.0 §13 layout ----- */

static void config_dma_stream(DMA_HandleTypeDef *hdma, DMA_Stream_TypeDef *inst,
                              uint32_t request) {
    hdma->Instance = inst;
    hdma->Init.Request = request;
    hdma->Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma->Init.PeriphInc = DMA_PINC_DISABLE;
    hdma->Init.MemInc = DMA_MINC_ENABLE;
    hdma->Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma->Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma->Init.Mode = DMA_CIRCULAR;
    hdma->Init.Priority = DMA_PRIORITY_HIGH;
    hdma->Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(hdma) != HAL_OK) Error_Handler();
}

static void uart_gpio_init(GPIO_TypeDef *tx_port, uint16_t tx_pin,
                           GPIO_TypeDef *rx_port, uint16_t rx_pin,
                           uint8_t af) {
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = af;
    g.Pin = tx_pin;  HAL_GPIO_Init(tx_port, &g);
    g.Pin = rx_pin;  HAL_GPIO_Init(rx_port, &g);
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        uart_gpio_init(USART1_TX_Port, USART1_TX_Pin,
                       USART1_RX_Port, USART1_RX_Pin, GPIO_AF7_USART1);
        config_dma_stream(&hdma_usart1_rx, DMA1_Stream0, DMA_REQUEST_USART1_RX);
        __HAL_LINKDMA(huart, hdmarx, hdma_usart1_rx);
        HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    } else if (huart->Instance == USART2) {
        __HAL_RCC_USART2_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        uart_gpio_init(USART2_TX_Port, USART2_TX_Pin,
                       USART2_RX_Port, USART2_RX_Pin, GPIO_AF7_USART2);
        config_dma_stream(&hdma_usart2_rx, DMA1_Stream1, DMA_REQUEST_USART2_RX);
        __HAL_LINKDMA(huart, hdmarx, hdma_usart2_rx);
        HAL_NVIC_SetPriority(USART2_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART2_IRQn);
    } else if (huart->Instance == USART3) {
        __HAL_RCC_USART3_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        uart_gpio_init(USART3_TX_Port, USART3_TX_Pin,
                       USART3_RX_Port, USART3_RX_Pin, GPIO_AF7_USART3);
        /* No DMA / IRQ — ST-LINK VCP debug only. */
    } else if (huart->Instance == UART4) {
        __HAL_RCC_UART4_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        uart_gpio_init(UART4_TX_Port, UART4_TX_Pin,
                       UART4_RX_Port, UART4_RX_Pin, GPIO_AF8_UART4);
        config_dma_stream(&hdma_uart4_rx, DMA1_Stream2, DMA_REQUEST_UART4_RX);
        __HAL_LINKDMA(huart, hdmarx, hdma_uart4_rx);
        HAL_NVIC_SetPriority(UART4_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(UART4_IRQn);
    } else if (huart->Instance == UART5) {
        __HAL_RCC_UART5_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        uart_gpio_init(UART5_TX_Port, UART5_TX_Pin,
                       UART5_RX_Port, UART5_RX_Pin, GPIO_AF14_UART5);
        config_dma_stream(&hdma_uart5_rx, DMA1_Stream3, DMA_REQUEST_UART5_RX);
        __HAL_LINKDMA(huart, hdmarx, hdma_uart5_rx);
        HAL_NVIC_SetPriority(UART5_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(UART5_IRQn);
    } else if (huart->Instance == USART6) {
        __HAL_RCC_USART6_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();
        uart_gpio_init(USART6_TX_Port, USART6_TX_Pin,
                       USART6_RX_Port, USART6_RX_Pin, GPIO_AF7_USART6);
        config_dma_stream(&hdma_usart6_rx, DMA1_Stream4, DMA_REQUEST_USART6_RX);
        __HAL_LINKDMA(huart, hdmarx, hdma_usart6_rx);
        HAL_NVIC_SetPriority(USART6_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART6_IRQn);
    } else if (huart->Instance == UART7) {
        __HAL_RCC_UART7_CLK_ENABLE();
        /* UART7 reserved spare — pins left unrouted in v1.0 */
    }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) { __HAL_RCC_USART1_CLK_DISABLE(); HAL_DMA_DeInit(huart->hdmarx); }
    else if (huart->Instance == USART2) { __HAL_RCC_USART2_CLK_DISABLE(); HAL_DMA_DeInit(huart->hdmarx); }
    else if (huart->Instance == USART3) { __HAL_RCC_USART3_CLK_DISABLE(); }
    else if (huart->Instance == UART4) { __HAL_RCC_UART4_CLK_DISABLE(); HAL_DMA_DeInit(huart->hdmarx); }
    else if (huart->Instance == UART5) { __HAL_RCC_UART5_CLK_DISABLE(); HAL_DMA_DeInit(huart->hdmarx); }
    else if (huart->Instance == USART6) { __HAL_RCC_USART6_CLK_DISABLE(); HAL_DMA_DeInit(huart->hdmarx); }
    else if (huart->Instance == UART7) { __HAL_RCC_UART7_CLK_DISABLE(); }
}

/* ----- I2C1 MSP ----- */

void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c) {
    if (hi2c->Instance == I2C1) {
        __HAL_RCC_I2C1_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        GPIO_InitTypeDef g = {0};
        g.Mode = GPIO_MODE_AF_OD;
        g.Pull = GPIO_PULLUP;
        g.Speed = GPIO_SPEED_FREQ_LOW;
        g.Alternate = GPIO_AF4_I2C1;
        g.Pin = I2C1_SCL_Pin; HAL_GPIO_Init(I2C1_SCL_Port, &g);
        g.Pin = I2C1_SDA_Pin; HAL_GPIO_Init(I2C1_SDA_Port, &g);
        HAL_NVIC_SetPriority(I2C1_EV_IRQn, 7, 0);
        HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
        HAL_NVIC_SetPriority(I2C1_ER_IRQn, 7, 0);
        HAL_NVIC_EnableIRQ(I2C1_ER_IRQn);
    }
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef *hi2c) {
    if (hi2c->Instance == I2C1) {
        __HAL_RCC_I2C1_CLK_DISABLE();
        HAL_NVIC_DisableIRQ(I2C1_EV_IRQn);
        HAL_NVIC_DisableIRQ(I2C1_ER_IRQn);
    }
}

/* ----- SDMMC1 MSP ----- */

void HAL_SD_MspInit(SD_HandleTypeDef *hsd) {
    if (hsd->Instance == SDMMC1) {
        __HAL_RCC_SDMMC1_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        GPIO_InitTypeDef g = {0};
        g.Mode = GPIO_MODE_AF_PP;
        g.Pull = GPIO_PULLUP;
        g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        g.Alternate = GPIO_AF12_SDIO1;
        g.Pin = SDMMC1_D0_Pin | SDMMC1_D1_Pin | SDMMC1_D2_Pin |
                SDMMC1_D3_Pin | SDMMC1_CK_Pin;
        HAL_GPIO_Init(GPIOC, &g);
        g.Pin = SDMMC1_CMD_Pin;
        HAL_GPIO_Init(SDMMC1_CMD_Port, &g);
        HAL_NVIC_SetPriority(SDMMC1_IRQn, 8, 0);
        HAL_NVIC_EnableIRQ(SDMMC1_IRQn);
    }
}

void HAL_SD_MspDeInit(SD_HandleTypeDef *hsd) {
    if (hsd->Instance == SDMMC1) {
        __HAL_RCC_SDMMC1_CLK_DISABLE();
        HAL_NVIC_DisableIRQ(SDMMC1_IRQn);
    }
}

/* ----- TIM2 MSP ----- */

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM2) {
        __HAL_RCC_TIM2_CLK_ENABLE();
    }
}

void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef *htim) {
    if (htim->Instance == TIM2) {
        __HAL_RCC_TIM2_CLK_DISABLE();
    }
}

/* ----- USB OTG_FS MSP ----- */

void HAL_PCD_MspInit(PCD_HandleTypeDef *hpcd) {
    if (hpcd->Instance == USB_OTG_FS) {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        GPIO_InitTypeDef g = {0};
        g.Mode = GPIO_MODE_AF_PP;
        g.Pull = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        g.Alternate = GPIO_AF10_OTG2_FS;
        g.Pin = USB_DM_Pin | USB_DP_Pin;
        HAL_GPIO_Init(USB_DM_Port, &g);

        __HAL_RCC_USB2_OTG_FS_CLK_ENABLE();
        HAL_PWREx_EnableUSBVoltageDetector();
        HAL_NVIC_SetPriority(OTG_FS_IRQn, 9, 0);
        HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
    }
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef *hpcd) {
    if (hpcd->Instance == USB_OTG_FS) {
        __HAL_RCC_USB2_OTG_FS_CLK_DISABLE();
        HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    }
}
