cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(branch_wifi5 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(branch_wifi5
    src/main.c
    src/core0_leaf_io.c
    src/core1_upstream.c
    src/pio_uart.c
    src/proto.c
    src/queues.c
    src/pps_time.c
    src/leaf_cmd.c
    src/leaf_health.c
    src/dedup.c
    src/wids.c
    src/upstream_fmt.c
)

target_include_directories(branch_wifi5 PRIVATE
    include
    src
    ${CMAKE_CURRENT_BINARY_DIR}
)

pico_generate_pio_header(branch_wifi5 ${CMAKE_CURRENT_LIST_DIR}/pio/uart_rx.pio)
pico_generate_pio_header(branch_wifi5 ${CMAKE_CURRENT_LIST_DIR}/pio/uart_tx.pio)

target_link_libraries(branch_wifi5
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_dma
    hardware_irq
    hardware_gpio
    hardware_uart
    hardware_timer
    hardware_sync
)

pico_enable_stdio_uart(branch_wifi5 0)
pico_enable_stdio_usb(branch_wifi5 0)

pico_add_extra_outputs(branch_wifi5)
