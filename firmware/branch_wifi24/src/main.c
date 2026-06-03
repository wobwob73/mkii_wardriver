#include "branch_defs.h"
#include "proto.h"
#include "queues.h"
#include "dedup.h"
#include "wids.h"
#include "pps_time.h"
#include "leaf_health.h"
#include "leaf_cmd.h"
#include "upstream_fmt.h"
#include "core0_leaf_io.h"
#include "core1_upstream.h"
#include "pio_uart.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

static void pps_irq_callback(unsigned int gpio, uint32_t events) {
    pps_time_isr(gpio, events);
}

static void core1_entry(void) {
    core1_init();
    core1_run();
}

int main(void) {
    set_sys_clock_khz(125000, true);

    detection_q_init();
    wids_q_init();
    dedup_init();
    wids_init();
    leaf_health_init();
    pps_time_init();
    upstream_init();

    pio_uart_subsys_init();

    gpio_set_irq_enabled_with_callback(PPS_GPIO, GPIO_IRQ_EDGE_RISE, true, pps_irq_callback);

    multicore_launch_core1(core1_entry);

    core0_init();
    core0_run();

    return 0;
}
