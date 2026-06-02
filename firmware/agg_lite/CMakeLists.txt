cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(agg_lite C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(agg_lite
    src/main.c
    src/core0_leaf_io.c
    src/core1_main.c
    src/pio_uart.c
    src/proto.c
    src/queues.c
    src/pps_time.c
    src/gps.c
    src/leaf_cmd.c
    src/leaf_health.c
    src/dedup_wifi.c
    src/dedup_ble.c
    src/upstream_fmt.c
    src/sd_log.c
    src/sd_spi_fatfs.c
    src/usb_cdc_mirror.c
    src/agg_state.c
)

# One version string, three places (CODE_STATUS.md convention): this define,
# include/agg_defs.h, and lite_aggregator_v1_0.md §12.2.
target_compile_definitions(agg_lite PRIVATE
    AGG_LITE_FW_VERSION="1.0.0"
    AGG_W24_SCANHOP=1            # expects WiFi24 Leaf scan-hop mode (v1.3, §15)
    AGG_BLE_BT_CLASSIC=0         # BT Classic deferred in light-duty
)

# --- SD backend selection (lite_aggregator_v1_0.md §9 / §14 item 5) --------
# Default "stub" links the documented placeholder backend so agg_lite.uf2
# builds in CI and the ring/session state machine is exercised. Drop the
# vendored FatFs-over-SPI library into third_party/pico_fatfs_spi/ and
# reconfigure with -DAGG_SD_BACKEND=fatfs to swap in durable writes.
if(NOT DEFINED AGG_SD_BACKEND)
    set(AGG_SD_BACKEND "stub")
endif()
if(AGG_SD_BACKEND STREQUAL "fatfs")
    target_compile_definitions(agg_lite PRIVATE AGG_SD_BACKEND_FATFS=1)
    # add_subdirectory(third_party/pico_fatfs_spi) and link it here.
endif()

target_include_directories(agg_lite PRIVATE
    include
    src
    ${CMAKE_CURRENT_BINARY_DIR}
)

pico_generate_pio_header(agg_lite ${CMAKE_CURRENT_LIST_DIR}/pio/uart_rx.pio)
pico_generate_pio_header(agg_lite ${CMAKE_CURRENT_LIST_DIR}/pio/uart_tx.pio)

target_link_libraries(agg_lite
    pico_stdlib
    pico_multicore
    pico_stdio_usb
    hardware_pio
    hardware_dma
    hardware_irq
    hardware_gpio
    hardware_uart
    hardware_spi
    hardware_timer
    hardware_sync
)

# USB CDC is the optional live mirror, driven directly by usb_cdc_mirror.c via
# stdio_usb. UART0 (GP12) is free for an optional debug console during bring-up.
pico_enable_stdio_uart(agg_lite 0)
pico_enable_stdio_usb(agg_lite 1)

pico_add_extra_outputs(agg_lite)
