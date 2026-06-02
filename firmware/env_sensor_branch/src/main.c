#include "env_defs.h"
#include "i2c_bus.h"
#include "pps_time.h"
#include "stm32_uart.h"
#include "queues.h"
#include "icm42688.h"
#include "lis3mdl.h"
#include "bmp390.h"
#include "scd41.h"
#include "sgp41.h"
#include "core0_fusion.h"
#include "core1_output.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

static void pps_irq(unsigned int gpio, uint32_t events) {
    pps_time_isr(gpio, events);
}

static bool g_imu_ok, g_mag_ok, g_baro_ok, g_scd_ok, g_sgp_ok;

static void core1_entry(void) {
    core1_output_init(g_imu_ok, g_mag_ok, g_baro_ok, g_scd_ok, g_sgp_ok);
    core1_output_run();
}

int main(void) {
    set_sys_clock_khz(125000, true);

    stm32_uart_init();
    pps_time_init();
    env_q_init();
    i2c_bus_init();

    sleep_ms(50);

    g_imu_ok  = icm42688_init();
    g_mag_ok  = lis3mdl_init();
    g_baro_ok = bmp390_init();
    g_scd_ok  = scd41_init();
    g_sgp_ok  = sgp41_init();
    if (g_sgp_ok) {
        sgp41_start_conditioning();
    }

    gpio_set_irq_enabled_with_callback(PPS_GPIO, GPIO_IRQ_EDGE_RISE, true, pps_irq);

    core0_fusion_init(g_imu_ok, g_mag_ok);

    multicore_launch_core1(core1_entry);

    core0_fusion_entry();
    return 0;
}
