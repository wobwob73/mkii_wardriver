/*
 * main_stub.c — Example showing where to plug app_main() into the
 * CubeMX-generated main.c. Do NOT use this file as-is in a project;
 * it has none of the HAL_Init / SystemClock_Config / MX_*_Init calls
 * that CubeMX generates from the .ioc file. Copy the highlighted
 * snippets into your real Core/Src/main.c and remove this stub from
 * the build.
 */

#include "main.h"   /* from CubeMX */
#include "app_main.h"

void SystemClock_Config(void);

int main(void) {
    HAL_Init();
    SystemClock_Config();

    /* MX_*_Init() calls from CubeMX go here, e.g.:
     *   MX_GPIO_Init();
     *   MX_DMA_Init();
     *   MX_USART1_UART_Init();
     *   MX_USART2_UART_Init();
     *   MX_UART4_Init();
     *   MX_UART5_Init();
     *   MX_USART6_UART_Init();
     *   MX_USART3_UART_Init();
     *   MX_I2C1_Init();
     *   MX_SDMMC1_SD_Init();
     *   MX_USB_OTG_FS_PCD_Init();
     *   MX_FATFS_Init();
     *   MX_TIM2_Init();
     *   MX_IWDG_Init();
     * Then start the DMA RX paths for each Branch UART in circular mode
     * (HAL_UART_Receive_DMA), and start TIM2 free-running at 1 MHz
     * (HAL_TIM_Base_Start).
     */

    app_main();

    while (1) {
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == GPIO_PIN_10) {
        extern void pal_pps_dispatch_from_isr(void);
        pal_pps_dispatch_from_isr();
    }
}

void Error_Handler(void) {
    __disable_irq();
    while (1) {
    }
}
