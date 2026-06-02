cmake_minimum_required(VERSION 3.20)

#
# MKII STM32 Aggregator firmware build.
#
# Two build modes:
#
#   1. Host smoke (default):
#        cmake -DMKII_STM32_UNIT=1 ..
#        make
#      Builds App/ + stub PAL into libmkii_stm32_app.a using the host
#      compiler. No HAL, no on-target image.
#
#   2. On-target ARM cross-compile:
#        cmake -DMKII_STM32_UNIT=1 -DMKII_STM32_TARGET=on \
#              -DCMAKE_TOOLCHAIN_FILE=Platform/arm-none-eabi.cmake ..
#        make
#      Fetches STM32CubeH7 v1.11.x via FetchContent (~600 MB shallow
#      clone, cached after first run), builds the full bring-up
#      (Core/, Platform/, USB CDC, FatFS, sd_diskio) against the HAL
#      drivers, and produces stm32_aggregator_unit{1,2}.{elf,bin,hex}.
#

option(MKII_STM32_TARGET "Build the on-target ARM cross image" OFF)

if(NOT DEFINED MKII_STM32_UNIT)
    set(MKII_STM32_UNIT 1)
endif()

project(mkii_stm32_aggregator C ASM)
set(CMAKE_C_STANDARD 11)

if(NOT MKII_STM32_TARGET)
    # -------------------- Host smoke build (App/ + stub PAL) --------------------
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
    return()
endif()

# -------------------- On-target ARM cross-compile build --------------------
#
# Requires CMAKE_TOOLCHAIN_FILE to point at an ARM bare-metal toolchain
# file (Platform/arm-none-eabi.cmake provides one for this repo).
#

set(MKII_STM32_CUBEH7_TAG "v1.11.2" CACHE STRING "STM32CubeH7 tag to fetch")

include(FetchContent)
FetchContent_Declare(
    stm32cubeh7
    GIT_REPOSITORY https://github.com/STMicroelectronics/STM32CubeH7.git
    GIT_TAG        ${MKII_STM32_CUBEH7_TAG}
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""
)
FetchContent_MakeAvailable(stm32cubeh7)

# STM32CubeH7's HAL drivers + CMSIS device live in git submodules. CMake's
# FetchContent disables those by default with GIT_SUBMODULES "", so init the
# two we need (and only those — the BSP submodules are large and unused).
find_program(GIT_EXEC git REQUIRED)
execute_process(
    COMMAND ${GIT_EXEC} submodule update --init --depth 1
            Drivers/STM32H7xx_HAL_Driver
            Drivers/CMSIS/Device/ST/STM32H7xx
    WORKING_DIRECTORY ${stm32cubeh7_SOURCE_DIR}
    RESULT_VARIABLE submod_rc
)
if(NOT submod_rc EQUAL 0)
    message(FATAL_ERROR "Failed to init STM32CubeH7 submodules (rc=${submod_rc})")
endif()

set(CUBEH7_ROOT ${stm32cubeh7_SOURCE_DIR})
set(HAL_SRC     ${CUBEH7_ROOT}/Drivers/STM32H7xx_HAL_Driver/Src)
set(CMSIS_DEV   ${CUBEH7_ROOT}/Drivers/CMSIS/Device/ST/STM32H7xx)
set(USB_LIB     ${CUBEH7_ROOT}/Middlewares/ST/STM32_USB_Device_Library)
set(FATFS_LIB   ${CUBEH7_ROOT}/Middlewares/Third_Party/FatFs/src)

# ---- HAL driver sources (minimal subset for what we use) ----
set(HAL_DRIVER_SRC
    ${HAL_SRC}/stm32h7xx_hal.c
    ${HAL_SRC}/stm32h7xx_hal_cortex.c
    ${HAL_SRC}/stm32h7xx_hal_dma.c
    ${HAL_SRC}/stm32h7xx_hal_dma_ex.c
    ${HAL_SRC}/stm32h7xx_hal_exti.c
    ${HAL_SRC}/stm32h7xx_hal_flash.c
    ${HAL_SRC}/stm32h7xx_hal_flash_ex.c
    ${HAL_SRC}/stm32h7xx_hal_gpio.c
    ${HAL_SRC}/stm32h7xx_hal_hsem.c
    ${HAL_SRC}/stm32h7xx_hal_i2c.c
    ${HAL_SRC}/stm32h7xx_hal_i2c_ex.c
    ${HAL_SRC}/stm32h7xx_hal_iwdg.c
    ${HAL_SRC}/stm32h7xx_hal_mdma.c
    ${HAL_SRC}/stm32h7xx_hal_pcd.c
    ${HAL_SRC}/stm32h7xx_hal_pcd_ex.c
    ${HAL_SRC}/stm32h7xx_hal_pwr.c
    ${HAL_SRC}/stm32h7xx_hal_pwr_ex.c
    ${HAL_SRC}/stm32h7xx_hal_rcc.c
    ${HAL_SRC}/stm32h7xx_hal_rcc_ex.c
    ${HAL_SRC}/stm32h7xx_hal_sd.c
    ${HAL_SRC}/stm32h7xx_hal_sd_ex.c
    ${HAL_SRC}/stm32h7xx_ll_sdmmc.c
    ${HAL_SRC}/stm32h7xx_hal_tim.c
    ${HAL_SRC}/stm32h7xx_hal_tim_ex.c
    ${HAL_SRC}/stm32h7xx_hal_uart.c
    ${HAL_SRC}/stm32h7xx_hal_uart_ex.c
    ${HAL_SRC}/stm32h7xx_ll_usb.c
)

# ---- USB Device Library + CDC class ----
set(USBD_SRC
    ${USB_LIB}/Core/Src/usbd_core.c
    ${USB_LIB}/Core/Src/usbd_ctlreq.c
    ${USB_LIB}/Core/Src/usbd_ioreq.c
    ${USB_LIB}/Class/CDC/Src/usbd_cdc.c
)

# ---- FatFS ----
# NOTE: FatFS's diskio.c + ff_gen_drv.c provide a driver-registry shim that
# delegates disk_* to a registered Driver. MKII's sd_diskio.c implements
# disk_* directly, so we skip the shim to avoid duplicate symbols.
set(FATFS_SRC
    ${FATFS_LIB}/ff.c
    ${FATFS_LIB}/option/syscall.c
    ${FATFS_LIB}/option/ccsbcs.c     # Single-byte code-page conversion (CP437 LFN support)
)

# ---- ST CMSIS + startup + system ----
set(CMSIS_SRC
    ${CMSIS_DEV}/Source/Templates/system_stm32h7xx.c
    ${CMSIS_DEV}/Source/Templates/gcc/startup_stm32h753xx.s
)

# ---- MKII App, PAL, Core, Platform glue ----
set(MKII_SRC
    App/proto.c
    App/pps_time.c
    App/gps.c
    App/branch_uart.c
    App/usb_cdc_app.c
    App/cmd_router.c
    App/dispatch.c
    App/sd_log.c
    App/app_main.c
    Platform/Src/pal_hal_stm32h7.c
    Core/Src/main.c
    Core/Src/stm32h7xx_hal_msp.c
    Core/Src/stm32h7xx_it.c
    Platform/USB/Src/usb_device.c
    Platform/USB/Src/usbd_conf.c
    Platform/USB/Src/usbd_desc.c
    Platform/USB/Src/usbd_cdc_if.c
    Platform/FatFS/Src/sd_diskio.c
)

set(TARGET_NAME stm32_aggregator_unit${MKII_STM32_UNIT})
add_executable(${TARGET_NAME}
    ${MKII_SRC}
    ${HAL_DRIVER_SRC}
    ${USBD_SRC}
    ${FATFS_SRC}
    ${CMSIS_SRC}
)

target_include_directories(${TARGET_NAME} PRIVATE
    App
    Core/Inc
    Platform/Inc
    Platform/USB/Inc
    Platform/FatFS/Inc
    ${CUBEH7_ROOT}/Drivers/STM32H7xx_HAL_Driver/Inc
    ${CUBEH7_ROOT}/Drivers/CMSIS/Device/ST/STM32H7xx/Include
    ${CUBEH7_ROOT}/Drivers/CMSIS/Include
    ${USB_LIB}/Core/Inc
    ${USB_LIB}/Class/CDC/Inc
    ${FATFS_LIB}
)

target_compile_definitions(${TARGET_NAME} PRIVATE
    USE_HAL_DRIVER
    STM32H753xx
    MKII_FW_VERSION="1.0.2"
    MKII_STM32_UNIT=${MKII_STM32_UNIT}
)

target_compile_options(${TARGET_NAME} PRIVATE
    -mcpu=cortex-m7
    -mfpu=fpv5-d16
    -mfloat-abi=hard
    -mthumb
    -fdata-sections -ffunction-sections
    -Wall
    -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable
    -fno-builtin
    $<$<COMPILE_LANGUAGE:C>:-std=gnu11>
    -O2 -g
)

target_link_options(${TARGET_NAME} PRIVATE
    -mcpu=cortex-m7
    -mfpu=fpv5-d16
    -mfloat-abi=hard
    -mthumb
    -T${CMAKE_CURRENT_SOURCE_DIR}/Platform/Linker/STM32H753ZITX_FLASH.ld
    -Wl,--gc-sections
    -Wl,-Map=${TARGET_NAME}.map,--cref
    -specs=nano.specs
    -specs=nosys.specs
    -static
    -Wl,--start-group -lc -lm -Wl,--end-group
)

set_target_properties(${TARGET_NAME} PROPERTIES SUFFIX ".elf")

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}> ${TARGET_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${TARGET_NAME}> ${TARGET_NAME}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
)
