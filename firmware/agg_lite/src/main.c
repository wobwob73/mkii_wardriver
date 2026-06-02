#include "agg_defs.h"
#include "queues.h"
#include "pps_time.h"
#include "leaf_health.h"
#include "pio_uart.h"
#include "core0_leaf_io.h"
#include "core1_main.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

/*
 * Light-Duty Single-Box Aggregator entry point (lite_aggregator_v1_0.md §3).
 *
 *   Core 0 — 3× PIO UART Leaf links (RX assembly, dispatch, TX), Leaf health
 *            watchdog + recovery, downstream $CF/$CH/$PG/$RB.
 *   Core 1 — 1PPS + GPS NMEA + local $TM; three dedup pipelines (W24/W5G/BLE);
 *            upstream record formatting; SD writer draining a RAM ring; $LA.
 */

static void pps_irq_callback(unsigned int gpio, uint32_t events) {
    pps_time_isr(gpio, events);
}

static void core1_entry(void) {
    core1_init();
    core1_run();
}

int main(void) {
    set_sys_clock_khz(125000, true);

    queues_init();
    leaf_health_init();
    pps_time_init();

    gpio_init(AGG_LED_PIN);
    gpio_set_dir(AGG_LED_PIN, GPIO_OUT);
    gpio_put(AGG_LED_PIN, 0);

    pio_uart_subsys_init();

    gpio_set_irq_enabled_with_callback(PPS_GPIO, GPIO_IRQ_EDGE_RISE, true,
                                       pps_irq_callback);

    multicore_launch_core1(core1_entry);

    core0_init();
    core0_run();

    return 0;
}
