# `stm32_aggregator` — STM32 Mid-Tier Aggregator Firmware

NUCLEO-H753ZI (STM32H753ZIT6) firmware for both STM32 units. A single source tree, two unit images, selected at compile time:

```
cmake -DMKII_STM32_UNIT=1 ..   # RF Collection (WiFi24, W5G, BLE, DOT154, SEN)
cmake -DMKII_STM32_UNIT=2 ..   # Sub-GHz / FPV (MTC, VHF, UHF, FPV)
```

Implements `stm32_h753_firmware_v1_0.md` — see that spec for the full operating-mode table, message routing, PPS + `$TM` emission contract, USB-CDC and SD-card behavior, and the test procedure.

## Repository layout

```
firmware/stm32_aggregator/
├── App/                          MKII business logic (portable C, PAL-driven)
│   ├── proto.{h,c}               $..*XX\n framing and checksum
│   ├── pps_time.{h,c}            edge capture + UTC association + $TM emit
│   ├── gps.{h,c}                 I2C NMEA reader for u-blox M10
│   ├── branch_uart.{h,c}         per-Branch line-receiver pump
│   ├── usb_cdc_app.{h,c}         USB CDC RX/TX with line assembly
│   ├── cmd_router.{h,c}          $RC leaf_id → Branch UART routing
│   ├── dispatch.{h,c}            Branch → USB CDC + SD log forwarding
│   ├── sd_log.{h,c}              FatFS session file management
│   └── app_main.{h,c}            top-level state machine and $AG heartbeat
│
├── Platform/                     Platform Abstraction Layer + on-target glue
│   ├── Inc/pal.h                 PAL contract (called by App/)
│   ├── Src/pal_hal.c             stub PAL backing (host-smoke build only)
│   ├── Src/pal_hal_stm32h7.c     real STM32H7 HAL-backed PAL (on-target build)
│   ├── USB/                      USB CDC device class glue (usbd_conf/desc/cdc_if)
│   ├── FatFS/                    ffconf.h + sd_diskio.c (SDMMC1 disk I/O)
│   ├── Linker/STM32H753ZITX_FLASH.ld
│   └── arm-none-eabi.cmake       ARM bare-metal toolchain file
│
├── Core/                         on-target entry point + HAL scaffold
│   ├── Inc/stm32_config.h        per-unit pin map + Branch table
│   ├── Inc/main.h                pin map + HAL handle externs
│   ├── Inc/stm32h7xx_hal_conf.h  HAL module enables + clock constants
│   ├── Src/main.c                SystemClock_Config + MX_*_Init + app_main() entry
│   ├── Src/stm32h7xx_hal_msp.c   per-peripheral MSP init (GPIO AF, DMA, NVIC)
│   └── Src/stm32h7xx_it.c        interrupt handlers
│
├── CMakeLists.txt                host-smoke (default) + on-target (MKII_STM32_TARGET=on)
└── README.md                     this file
```

## On-target build (implemented)

The HAL bring-up is **done in-tree** — there is no CubeMX checkout step. `Core/Src/main.c`
hand-codes `SystemClock_Config()` + every `MX_*_Init()`, `Platform/Src/pal_hal_stm32h7.c`
backs the PAL with real HAL calls, and the on-target CMake target fetches the STM32CubeH7
HAL/USB/FatFS via `FetchContent` (pinned tag). It produces a flashable image:

```
mkdir build-target && cd build-target
cmake -G Ninja \
  -DMKII_STM32_TARGET=on \
  -DMKII_STM32_UNIT=1 \
  -DCMAKE_TOOLCHAIN_FILE=../Platform/arm-none-eabi.cmake ..
ninja                                  # → stm32_aggregator_unit1.{elf,bin,hex}
```

Requires `arm-none-eabi-gcc` (12.x+) and network access for the first configure (CubeH7
shallow clone; cached afterward). CI builds both units on every push.

The clock tree is **480 MHz SYSCLK / 240 MHz AXI / 120 MHz APB** (`SystemClock_Config`),
with the I2C1 `TIMINGR` and TIM2 prescaler derived from the 120 MHz PCLK1 — see the inline
derivations in `main.c`. The pin/peripheral map below is the authoritative reference for
the hand-coded init (and for a CubeMX round-trip, if you ever want one):

   | Peripheral | Mode | Pins / Notes |
   |---|---|---|
   | RCC | HSE bypass (ST-LINK MCO) | NUCLEO HSE 8 MHz |
   | Clock | SYSCLK 480 MHz, AXI 240 MHz, APB1/2/3/4 120 MHz | VOS0 |
   | USART1 | Async, 230400 8N1, DMA RX circular | PA9 TX, PA10 RX |
   | USART2 | Async, 230400 8N1, DMA RX circular | PD5 TX, PD6 RX |
   | USART3 | Async, 115200 8N1 (ST-LINK VCP) | PD8 TX, PD9 RX — debug only |
   | UART4 | Async, 230400 8N1, DMA RX circular | PB9 TX, PB8 RX |
   | UART5 | Async, 230400 8N1, DMA RX circular | PB13 TX, PB12 RX |
   | USART6 | Async, 230400 8N1, DMA RX circular | PC6 TX, PC7 RX — STM32 #1 only |
   | I2C1 | Fast Mode 400 kHz | PB6 SCL, PB7 SDA |
   | SDMMC1 | 4-bit Wide Bus, default speed | PC8–PC12 + PD2 CMD |
   | USB_OTG_FS | Device-only, internal FS PHY | PA11 DM, PA12 DP |
   | TIM2 | Base Init, 1 MHz tick (prescaler 239 from 240 MHz APB1 timer clock) | free-running, used for `pal_time_us_64` |
   | EXTI10 | Rising edge, NVIC enabled | PG10 (PPS input) |
   | IWDG | Window: 8 s | optional but recommended |
   | LEDs | Output PB0 (LD1), PE1 (LD2), PB14 (LD3) | |

The `Core/` + `Platform/` sources already encode all of the above (peripheral modes,
DMA streams, NVIC priorities, USB CDC, FatFS). There is no `.ioc` in the repo; the
equivalent state lives in `main.c` + `stm32h7xx_hal_msp.c`. If you ever want to
round-trip through CubeMX, import the project back rather than opening an `.ioc`, and
mirror any changes into those two files.

## GPS pre-configuration (required before bench use)

`App/gps.c::gps_push_config()` is intentionally a **no-op** in v1.0.x — the firmware does
not push UBX configuration at boot. The u-blox M10 module must be pre-configured once via
**u-center** (settings persist in BBR/Flash):

- **NMEA output:** enable `RMC` + `GGA` at 1 Hz on the **I2C (DDC)** port; disable `GSV`,
  `GSA`, `GLL`, `VTG`, `GNS` to keep the I2C read loop light.
- **PPS (TIMEPULSE):** 1 Hz, ~100 ms pulse width, **rising edge UTC-aligned**, enabled
  only when the module has a fix.

Until then `$TM`/PPS timing is unavailable. Sending these as UBX `CFG-VALSET` frames at
boot is tracked as a v1.1 item (STM32 spec §14 item 2). See the open-items register there.

## Host smoke build (no HAL)

The CMakeLists.txt in this directory builds App/ + the stub PAL into a static library so the C is compile-checked without needing the HAL:

```
mkdir build && cd build
cmake -DMKII_STM32_UNIT=1 ..
make
```

This catches type and link errors in the portable layer early. It does **not** verify peripheral behavior — that requires the HAL build above.

## Notes

- The PAL keeps the MKII business logic portable: in principle the same App/ tree could be retargeted to a different MCU (e.g., a Teensy 4.1 or a second RP2040) by writing a new PAL backing. STM32H7 is the v1.0 platform.
- ST-LINK VCP on USART3 is the recommended debug console during bring-up — `pal_usb_cdc_attached()` returns true only after a real USB host enumerates the OTG_FS port, so the unit boots in Standalone mode by default until the Trunk attaches.
- The IWDG kick happens inside the main loop in `app_main.c`. If you observe IWDG resets during long SD-card writes, increase the IWDG window or add a kick inside `sd_log_tick()`.
- See `MKII_handoff/specs/stm32_h753_firmware_v1_0.md` for the full design + the bench-test procedure (§13).
