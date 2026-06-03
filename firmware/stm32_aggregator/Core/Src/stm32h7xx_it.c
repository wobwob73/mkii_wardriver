/*
 * stm32h7xx_it.c — Interrupt handlers.
 *
 * Most handlers forward into HAL_*_IRQHandler routines so the HAL state
 * machines stay in sync. EXTI dispatch and TIM2 overflow both ultimately
 * surface in main.c callbacks.
 */

#include "main.h"
#include "stm32h7xx_hal.h"

/* ----- Core fault handlers ----- */

void NMI_Handler(void) {
    HAL_RCC_NMI_IRQHandler();
}

void HardFault_Handler(void) {
    while (1) { }
}
void MemManage_Handler(void) { while (1) { } }
void BusFault_Handler(void)  { while (1) { } }
void UsageFault_Handler(void){ while (1) { } }

void SVC_Handler(void) { }
void DebugMon_Handler(void) { }
void PendSV_Handler(void) { }
void SysTick_Handler(void) { HAL_IncTick(); }

/* ----- DMA streams 0..4 = Branch UART RX channels ----- */

void DMA1_Stream0_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_usart1_rx); }
void DMA1_Stream1_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_usart2_rx); }
void DMA1_Stream2_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_uart4_rx); }
void DMA1_Stream3_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_uart5_rx); }
void DMA1_Stream4_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_usart6_rx); }

/* ----- USART/UART error handlers — flush ORE/FE/NE/PE ----- */

void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&huart1); }
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&huart2); }
void UART4_IRQHandler(void)  { HAL_UART_IRQHandler(&huart4); }
void UART5_IRQHandler(void)  { HAL_UART_IRQHandler(&huart5); }
void USART6_IRQHandler(void) { HAL_UART_IRQHandler(&huart6); }

/* ----- I2C1 ----- */

void I2C1_EV_IRQHandler(void) { HAL_I2C_EV_IRQHandler(&hi2c1); }
void I2C1_ER_IRQHandler(void) { HAL_I2C_ER_IRQHandler(&hi2c1); }

/* ----- SDMMC1 ----- */

void SDMMC1_IRQHandler(void) { HAL_SD_IRQHandler(&hsd1); }

/* ----- TIM2: every 32-bit overflow @ 1 MHz (every ~71.6 min) ----- */

void TIM2_IRQHandler(void) { HAL_TIM_IRQHandler(&htim2); }

/* ----- EXTI dispatch ----- */

void EXTI15_10_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(PPS_INPUT_Pin);
}

/* ----- USB OTG_FS ----- */

void OTG_FS_IRQHandler(void) { HAL_PCD_IRQHandler(&hpcd_USB_OTG_FS); }
