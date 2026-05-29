cmake_minimum_required(VERSION 3.20)

#
# Standalone CMake configuration for the MKII STM32 aggregator firmware
# (App/ layer only). This file builds the App/ business logic against
# a stub PAL into a host-runnable static library so the code can be
# compile-checked and unit-tested without the CubeMX-generated HAL.
#
# For the on-target build, integrate App/ + Platform/ + Core/ into your
# CubeMX project per README.md §HAL bring-up — that workflow uses
# STM32CubeIDE (or its makefile equivalent) and is not driven by this
# CMakeLists.
#
# Usage (host smoke build):
#   mkdir -p build && cd build
#   cmake -DMKII_STM32_UNIT=1 ..
#   make
#

project(mkii_stm32_aggregator C)
set(CMAKE_C_STANDARD 11)

if(NOT DEFINED MKII_STM32_UNIT)
    set(MKII_STM32_UNIT 1)
endif()

add_library(mkii_stm32_app STATIC
    App/proto.c
    App/pps_time.c
    App/gps.c
    App/branch_uart.c
    App/usb_cdc_app.c
    App/cmd_router.c
    App/dispatch.c
    App/sd_log.c
    App/app_main.c
    Platform/Src/pal_hal.c
)

target_include_directories(mkii_stm32_app PUBLIC
    App
    Core/Inc
    Platform/Inc
)

target_compile_definitions(mkii_stm32_app PUBLIC
    MKII_STM32_UNIT=${MKII_STM32_UNIT}
)

target_compile_options(mkii_stm32_app PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
)
