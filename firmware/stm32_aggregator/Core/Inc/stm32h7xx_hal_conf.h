#pragma once

/* Define which HAL modules we use. Modules not listed here are excluded from
 * the build to keep code size and link time down. */

#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_HSEM_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_IWDG_MODULE_ENABLED
#define HAL_MDMA_MODULE_ENABLED
#define HAL_PCD_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_SD_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

#define USE_HAL_DRIVER

#define HSE_VALUE      ((uint32_t)8000000U)   /* NUCLEO-H753ZI HSE crystal */
#define HSE_STARTUP_TIMEOUT  ((uint32_t)100U)
#define CSI_VALUE      ((uint32_t)4000000U)
#define HSI_VALUE      ((uint32_t)64000000U)
#define LSI_VALUE      ((uint32_t)32000U)
#define LSE_VALUE      ((uint32_t)32768U)
#define LSE_STARTUP_TIMEOUT  ((uint32_t)5000U)
#define EXTERNAL_CLOCK_VALUE  ((uint32_t)12288000U)

#define VDD_VALUE              ((uint32_t)3300U)
#define TICK_INT_PRIORITY      ((uint32_t)0x0FU)
#define USE_RTOS               0U
#define PREFETCH_ENABLE        1U
#define USE_SD_TRANSCEIVER     0U
#define USE_HAL_SD_REGISTER_CALLBACKS 0U
#define USE_HAL_UART_REGISTER_CALLBACKS 0U
#define USE_HAL_I2C_REGISTER_CALLBACKS 0U
#define USE_HAL_PCD_REGISTER_CALLBACKS 0U
#define USE_HAL_TIM_REGISTER_CALLBACKS 0U

#define USE_SPI_CRC                  0U
#define DATA_CACHE_ENABLE            1U
#define INSTRUCTION_CACHE_ENABLE     1U

#define USE_FULL_ASSERT              0U

#include "stm32h7xx_hal_rcc.h"
#include "stm32h7xx_hal_gpio.h"
#include "stm32h7xx_hal_dma.h"
#include "stm32h7xx_hal_mdma.h"
#include "stm32h7xx_hal_cortex.h"
#include "stm32h7xx_hal_flash.h"
#include "stm32h7xx_hal_pwr.h"
#include "stm32h7xx_hal_exti.h"
#include "stm32h7xx_hal_uart.h"
#include "stm32h7xx_hal_i2c.h"
#include "stm32h7xx_hal_sd.h"
#include "stm32h7xx_hal_pcd.h"
#include "stm32h7xx_hal_tim.h"
#include "stm32h7xx_hal_iwdg.h"
#include "stm32h7xx_hal_hsem.h"

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line);
#endif
