cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(env_sensor_branch C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(env_sensor_branch
    src/main.c
    src/core0_fusion.c
    src/core1_output.c
    src/proto.c
    src/queues.c
    src/pps_time.c
    src/stm32_uart.c
    src/i2c_bus.c
    src/icm42688.c
    src/lis3mdl.c
    src/bmp390.c
    src/scd41.c
    src/sgp41.c
    third_party/MadgwickAHRS.c
)

target_include_directories(env_sensor_branch PRIVATE
    include
    src
    third_party
)

target_link_libraries(env_sensor_branch
    pico_stdlib
    pico_multicore
    hardware_i2c
    hardware_uart
    hardware_gpio
    hardware_irq
    hardware_timer
    hardware_sync
)

pico_enable_stdio_uart(env_sensor_branch 0)
pico_enable_stdio_usb(env_sensor_branch 0)

pico_add_extra_outputs(env_sensor_branch)
