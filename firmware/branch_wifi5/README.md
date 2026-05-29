# `branch_wifi5` — 5 GHz WiFi Branch Controller Firmware

RP2040 firmware for the 5 GHz WiFi Branch Controller. Dual-core, aggregates 2–3 ESP32-C5 Leaves (`W5_1`, `W5_2`, `W5_3`) via PIO UART, deduplicates AP detections in 500 ms windows with tombstone reclaim, runs WIDS analysis (evil twin, deauth flood), and streams formatted records upstream to STM32 #1 USART2 via hardware UART0. Branch ID is `W5G`.

Implements `wifi5_branch_v1_0.md` §8–§13. The dual-core architecture, SPSC queue pattern (`__dmb()` barriers, no spinlocks), tombstone dedup, and PIO0=RX/PIO1=TX with OUT-pin remap are inherited from `branch_controller_wifi24_v1_0.md` + v1.1 amendment.

## Target

- **MCU:** RP2040
- **SDK:** pico-sdk + CMake (not Arduino — needs `multicore_launch_core1()`, PIO, DMA, hardware sync).
- **Build target:** `branch_wifi5.uf2`.

## Build

```
mkdir build && cd build
cmake -DPICO_BOARD=pico ..
make -j$(nproc)         # → build/branch_wifi5.uf2
```

Same toolchain as `branch_wifi24`. The pico-sdk is found via `PICO_SDK_PATH`.

## Pin map

| Pin | Function |
|---|---|
| GP0 / GP1 | Leaf W5_1 TX / RX |
| GP2 / GP3 | Leaf W5_2 TX / RX |
| GP4 / GP5 | Leaf W5_3 TX / RX |
| GP10 | 1PPS input (EXTI) |
| GP12 / GP13 | STM32 UART0 TX / RX |
| GP16 / GP17 | UART1 (optional debug console) |

PIO0 hosts 3 RX state machines (SM3 unused). PIO1 hosts 1 TX state machine with OUT-pin remap across the three Leaves. If `W5_3` is omitted from the physical assembly, the BC tolerates it — `$BS` reports `w5_3_st=0` (offline) and the dedup + WIDS paths continue with whichever Leaves did come up.

## Module layout

Mirrors `branch_wifi24/`:

```
branch_wifi5/
├── CMakeLists.txt
├── pico_sdk_import.cmake
├── README.md
├── include/branch_defs.h        W5G + N_LEAVES=3 + 5 GHz default channel-set IDs
├── pio/
│   ├── uart_rx.pio              (verbatim from branch_wifi24)
│   └── uart_tx.pio              (verbatim from branch_wifi24)
└── src/
    ├── main.c                   (verbatim from branch_wifi24)
    ├── core0_leaf_io.{h,c}      3-SM dispatch
    ├── core1_upstream.{h,c}     (verbatim from branch_wifi24)
    ├── pio_uart.{h,c}           PIO0 RX SMs + PIO1 TX SM with pin remap
    ├── proto.{h,c}              (verbatim)
    ├── queues.{h,c}             (verbatim)
    ├── pps_time.{h,c}           (verbatim)
    ├── leaf_cmd.{h,c}           $CF channel_set_id + $CH lo/hi 32-bit mask
    ├── leaf_health.{h,c}        (verbatim — loops over N_LEAVES)
    ├── dedup.{h,c}              (verbatim — channel field already uint8_t)
    ├── wids.{h,c}               (verbatim)
    └── upstream_fmt.{h,c}       branch_id="W5G", $BS with 3 leaf-status fields
```

## Build flags (knobs)

| Macro | Default | Source |
|---|---|---|
| `BRANCH_ID` | `"W5G"` | `branch_defs.h` |
| `N_LEAVES` | 3 | `branch_defs.h` |
| `DEDUP_CAP` | 512 | `branch_defs.h` |
| `DEDUP_WINDOW_US` | 500000 (500 ms) | `branch_defs.h` |
| `DEAUTH_FLOOD_THRESHOLD` | 10 events / 5 s | `branch_defs.h` |

## Notes

- The dedup `channel` field is `uint8_t`, which comfortably holds 5 GHz channel numbers 36–165.
- `$RC` `leaf_id` namespace is `W5_1` / `W5_2` / `W5_3`. Anything outside this set is dropped silently and counted toward `err_count`.
- Locally smoke-built clean against pico-sdk 2.1.0: 34 KB text, 69 KB BSS, 68 KB UF2.
