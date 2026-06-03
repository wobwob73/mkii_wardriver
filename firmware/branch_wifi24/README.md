# `branch_wifi24` — 2.4 GHz WiFi Branch Controller Firmware

RP2040 firmware for the 2.4 GHz WiFi Branch Controller. Dual-core. Aggregates four ESP32-C3 Leaves (W1–W4) via PIO UART, deduplicates AP detections in 500 ms windows, runs WIDS analysis (evil twin, deauth flood), and streams formatted records upstream to STM32 #1 USART1 via hardware UART0.

Implements `branch_controller_wifi24_v1_0.md` with v1.1 and v1.2 amendments applied (v1.1: true SPSC with `__dmb()` barriers, PIO0=RX/PIO1=TX split, tombstone-based dedup reclaim, `enc` field range 0–10; v1.2: `$RC` inner command is **hex-encoded** to remove `*`/`,` collisions with outer framing, PIO TX SM is properly retargeted via `PINCTRL` rewrite with self-check, PPS time state is read via an atomic snapshot, proto checksum validator rejects trailing data).

## Target

- **MCU:** RP2040 (any board variant).
- **SDK:** pico-sdk + CMake; not Arduino — requires `multicore_launch_core1()`, PIO, DMA channels, hardware sync.
- **Build target:** `branch_wifi24.uf2`.

## Build

One-time toolchain + SDK setup:

```
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi \
                 libstdc++-arm-none-eabi-newlib build-essential git
git clone -b master https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk
cd ~/pico-sdk && git submodule update --init
export PICO_SDK_PATH=$HOME/pico-sdk
```

Build:

```
mkdir build && cd build
cmake -DPICO_BOARD=pico ..
make -j$(nproc)         # → build/branch_wifi24.uf2
```

Flash: unplug → hold BOOTSEL → plug USB → release → `RPI-RP2` mass-storage appears → `cp build/branch_wifi24.uf2 /media/$USER/RPI-RP2/`.

For dual-core bring-up, use SWD via a second RP2040 running `debugprobe`. UART1 (GP16 TX / GP17 RX) is left free for an optional debug-console probe; enable `pico_enable_stdio_uart(branch_wifi24 1)` in `CMakeLists.txt` for `printf` output during development.

Both `pico_enable_stdio_uart` and `pico_enable_stdio_usb` are **0** by default: UART0 is the STM32 link, USB is reserved.

## File layout

```
branch_wifi24/
├── CMakeLists.txt
├── pico_sdk_import.cmake
├── README.md
├── include/branch_defs.h
├── pio/
│   ├── uart_rx.pio
│   └── uart_tx.pio
└── src/
    ├── main.c                  entry, core launch, init
    ├── core0_leaf_io.{h,c}     PIO UART line assembly, dispatch, watchdog
    ├── core1_upstream.{h,c}    dedup flush, WIDS, $TM/$RC ingest, upstream TX
    ├── pio_uart.{h,c}          PIO UART driver (PIO0=RX, PIO1=TX with pin remap)
    ├── proto.{h,c}             framing, checksum, field parsing, hex enc/dec
    ├── queues.{h,c}             SPSC ring buffers (true SPSC + __dmb)
    ├── pps_time.{h,c}           1PPS ISR, $TM parse, timestamp compute
    ├── leaf_cmd.{h,c}           downstream $CF/$CH/$PG/$RB + $RC relay
    ├── leaf_health.{h,c}        per-Leaf state tracking, watchdog, recovery
    ├── dedup.{h,c}              AP dedup w/ tombstone reclaim
    ├── wids.{h,c}               evil twin + deauth flood
    └── upstream_fmt.{h,c}       $WA / $WP / $ET / $DF / $BS
```

## Pin map

| Pin | Function |
|---|---|
| GP0 / GP1 | Leaf W1 TX / RX |
| GP2 / GP3 | Leaf W2 TX / RX |
| GP4 / GP5 | Leaf W3 TX / RX |
| GP6 / GP7 | Leaf W4 TX / RX |
| GP10 | 1PPS input (EXTI) |
| GP12 / GP13 | STM32 UART0 TX / RX |
| GP16 / GP17 | UART1 (optional debug console) |

## Notes

- Re-derivation of the v1.0 source described in `MKII_handoff/CODE_STATUS.md`. Original source was lost with the prior sandbox; this implementation follows `specs/branch_controller_wifi24_v1_0.md` with the v1.1 amendment.
- The DMA RX channels use a 128-word ring per Leaf with a continuous-mode configuration. The transfer count is set to the maximum, giving ~41 hours of continuous operation before a firmware reboot is required if no other reset fires. The leaf watchdog typically recovers individual stuck Leaves long before this.
