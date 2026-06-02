cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(branch_wifi24 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(branch_wifi24
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

target_include_directories(branch_wifi24 PRIVATE
    include
    src
    ${CMAKE_CURRENT_BINARY_DIR}
)

pico_generate_pio_header(branch_wifi24 ${CMAKE_CURRENT_LIST_DIR}/pio/uart_rx.pio)
pico_generate_pio_header(branch_wifi24 ${CMAKE_CURRENT_LIST_DIR}/pio/uart_tx.pio)

target_link_libraries(branch_wifi24
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

# UART0 is reserved for the STM32 link; USB is reserved for future use.
# Enable UART1 on GP16/GP17 for printf during bring-up if needed.
pico_enable_stdio_uart(branch_wifi24 0)
pico_enable_stdio_usb(branch_wifi24 0)

pico_add_extra_outputs(branch_wifi24)
