# MKII — Code Status & Firmware Inventory

**Compiled:** 2026-05-28
**Scope:** Every piece of code produced for MKII so far, where it lives, how it
builds and flashes, and the deliberate deviations from the spec documents that are
baked into each implementation.

> **Why the source isn't in this package.** Both firmware archives were generated in
> prior working sessions and written to a sandbox `outputs/` directory that is wiped
> between sessions. They are complete and known-good; they are simply not reachable
> from where this package was assembled. Pull them from the source conversations
> (URLs below) and commit them. The specs in `specs/` are the *design*; these archives
> are the *implementation*, and the two differ in documented ways (see deviation lists).

---

## 1. `leaf_wifi24` — 2.4 GHz WiFi Leaf firmware

| | |
|---|---|
| **Target** | ESP32-C3 (default board `seeed_xiao_esp32c3`) |
| **Framework** | Arduino via PlatformIO |
| **Implements** | Leaf Protocol spec **v1.1.0** (`specs/wifi24_leaf_protocol_v1_1.md`) |
| **Version string** | `1.1.0` (build flag `LEAF_VERSION`, fallback in `leaf_defs.h`) |
| **Status** | **Compiles and flashes successfully** (confirmed in VSCode/PlatformIO) |
| **Archive** | `leaf_wifi24.zip` |
| **Source chat** | "ESP32-C6 WiFi development code" — `https://claude.ai/chat/a5d7ac7c-7e6f-42e7-bb18-585dff6cad32` (final `present_files` output) |

A **single binary** runs on all four Leaves. Each Leaf adopts its `leaf_id` from the
first `$CF` it receives on its dedicated PIO UART. W1/W2/W3 = scan (channels 1/6/11),
W4 = WIDS (promiscuous, channel hopping). If no `$CF` arrives within 10 s, the Leaf
falls back to scan mode on channel 1 under `leaf_id = W?`.

### File layout

```
leaf_wifi24/
├── platformio.ini              build config, version, all -D flags
├── README.md                   layout, build, flagged deviations
├── include/
│   └── leaf_defs.h             shared enums, limits, link UART, WidsEvent struct
└── src/
    ├── main.cpp                setup()/loop(), boot announce, mode dispatch
    ├── uart_proto.{h,cpp}      $..*XX\n framing, XOR checksum, line receiver
    ├── config.{h,cpp}          $CF/$CH state, identity adoption
    ├── heartbeat.{h,cpp}       $HB emission, err/scan counters, heap watchdog
    ├── cmd_handler.{h,cpp}     dispatch CF/CH/PG/RB, mode switching
    ├── wifi_scan.{h,cpp}       async PASSIVE scan (W1/W2/W3)
    ├── wids_monitor.{h,cpp}    promiscuous + ring buffer + channel hop (W4)
    ├── frame_parser.{h,cpp}    802.11 mgmt frame decode + RSN/AKM walker
    └── bssid_tracker.{h,cpp}   FNV-1a open-addressing seen-set, 512 slots
```

Module mapping matches spec §10.2. `ARDUINO_USB_CDC_ON_BOOT=0`, so `Serial` is
hardware UART0 (the Branch link), not USB-CDC.

### Build / flash

```bash
pio run -e leaf_wifi24                # build
pio run -e leaf_wifi24 -t upload      # flash via USB
pio device monitor -b 230400          # observe link traffic
```

> **Known gotcha (already hit):** a project path containing parentheses — e.g. a
> browser duplicate-download named `leaf_wifi24(1)/` — breaks PlatformIO's upload
> because `dash` reads `(` as subshell syntax. Rename the directory. Also, if `pio`/
> `esptool` aren't on PATH (PlatformIO installed inside the VSCode extension), they
> live at `~/.platformio/penv/bin/pio` and
> `~/.platformio/packages/tool-esptoolpy/esptool.py`.

### Build flags (knobs)

| Flag | Default | Spec ref |
|---|---|---|
| `MAX_LINE_LEN` | 200 | §2.1 |
| `HB_INTERVAL_MS` | 10000 | §3.6 |
| `CONFIG_TIMEOUT_MS` | 10000 | §5.2 |
| `SCAN_FAIL_RESET_THRESHOLD` | 10 | §8.4 |
| `SCAN_FAIL_REBOOT_THRESHOLD` | 50 | §8.4 |
| `HEAP_MIN_BYTES` | 16384 | §8.5 |
| `WIDS_RING_SIZE` | 64 | §7.4 |
| `WIDS_SEEN_BSSID_MAX` | 512 | §7.7 |

### Deviations from spec v1.1.0 (candidates for a v1.2 amendment)

1. **WIDS ring overflow drops the *newest* event, not the oldest** (`wids_monitor.cpp`).
   Spec §7.4 says overwrite oldest. Implementation keeps already-buffered events and
   drops the incoming one. Functionally equivalent for loss accounting; differs in
   which event survives a burst.
2. **Encryption enum needs two more values.** Recommend adding `LE_OWE = 9` and
   `LE_WPA3_ENT = 10` in v1.2 — the RSN/AKM walker can distinguish OWE (Enhanced Open)
   and WPA3-Enterprise, but the current enum (§3.1.1, 0–8) can't represent them.
3. **Passive vs. active scan timing.** Spec §6.4 quotes 100–120 ms/channel, which is
   *active* scan timing. Per the passive listen-only design philosophy, the firmware
   uses passive scan at ~200 ms/channel (needs ≥~102 ms to catch one beacon interval).
   Net `$AP`/`$BK` output rate is ~60% of the spec example's assumption. Clarify §6.4
   to split active vs. passive in v1.2.

---

## 2. `branch_wifi24` — 2.4 GHz WiFi Branch Controller firmware

| | |
|---|---|
| **Target** | RP2040 |
| **Framework** | pico-sdk + CMake (NOT Arduino — needs `multicore_launch_core1()`, PIO, DMA chaining, hardware sync) |
| **Implements** | BC firmware guide **v1.0.0** (`specs/branch_controller_wifi24_v1_0.md`) |
| **Size** | 29 files, ~3,100 lines |
| **Status** | **Builds to `branch_wifi24.uf2`** |
| **Archive** | `branch_wifi24.zip` |
| **Source chat** | "ESP32-C6 WiFi development code" — `https://claude.ai/chat/a5d7ac7c-7e6f-42e7-bb18-585dff6cad32` (`present_files` output) |

Dual-core. **Core 0:** 4× PIO UART Leaf links with self-chaining DMA ring buffers,
line assembly, message dispatch, Leaf health watchdog, downstream command TX.
**Core 1:** 1PPS ISR + `$TM` parsing + timestamp computation, AP deduplication
(500 ms windows), WIDS analysis (evil-twin + deauth-flood), upstream TX to STM32.

### File layout (per spec §9)

```
branch_wifi24/
├── CMakeLists.txt              SDK project config, libs, PIO header generation
├── pico_sdk_import.cmake       standard SDK finder (from pico-examples)
├── README.md                   layout, build, flagged deviations
├── include/
│   └── branch_defs.h           constants, structs, enums
├── pio/
│   ├── uart_rx.pio             PIO UART RX program (loaded into PIO0)
│   └── uart_tx.pio             PIO UART TX program (loaded into PIO1)
└── src/
    ├── main.c                  entry, core launch, init sequence, state machine
    ├── core0_leaf_io.{h,c}     Core 0: PIO UART mgmt, line assembly, dispatch
    ├── core1_upstream.{h,c}    Core 1: main loop, dedup flush, WIDS, upstream TX
    ├── pio_uart.{h,c}          PIO UART driver (init, DMA, read/write)
    ├── proto.{h,c}             framing, checksum, field parse, hex enc/dec
    ├── queues.{h,c}            inter-core SPSC ring buffers
    ├── pps_time.{h,c}          1PPS ISR, $TM parse, timestamp compute
    ├── leaf_cmd.{h,c}          downstream command construction (CF/CH/PG/RB)
    ├── leaf_health.{h,c}       Leaf state tracking, watchdog, recovery
    ├── dedup.{h,c}             AP deduplication hash table
    ├── wids.{h,c}              evil-twin detection, deauth-flood tracking
    └── upstream_fmt.{h,c}      upstream formatting (WA/WP/ET/DF/BS)
```

### Build / flash

One-time toolchain + SDK:

```bash
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi \
                 libstdc++-arm-none-eabi-newlib build-essential git
git clone -b master https://github.com/raspberrypi/pico-sdk.git ~/pico-sdk
cd ~/pico-sdk && git submodule update --init
echo 'export PICO_SDK_PATH=$HOME/pico-sdk' >> ~/.bashrc && source ~/.bashrc
```

Build:

```bash
unzip branch_wifi24.zip && cd branch_wifi24
mkdir build && cd build
cmake -DPICO_BOARD=pico ..
make -j$(nproc)         # → build/branch_wifi24.uf2
```

Flash (drag-and-drop): unplug RP2040 → hold BOOTSEL → plug USB → release →
`RPI-RP2` drive mounts → `cp branch_wifi24.uf2 /media/$USER/RPI-RP2/`. Board reboots
into firmware. Re-enter BOOTSEL without unplugging via `picotool reboot -u -f`.

**VSCode path:** install the official **Raspberry Pi Pico** extension (bundles SDK/
toolchain/CMake/OpenOCD) → Command Palette → "Raspberry Pi Pico: Import Project" →
point at `branch_wifi24/`. Status-bar buttons: Compile, Run (BOOTSEL), Run/Debug
(Picoprobe over SWD). **For dual-core bring-up, use SWD** via a second RP2040 flashed
with `debugprobe` — a hung Core 1 is otherwise invisible:

```
debugprobe        target RP2040
GP2   ─────────→  SWCLK
GP3   ←────────→  SWDIO
GND   ─────────   GND
GP4(TX) ───────→  UART1 RX (GP17, optional debug console)
GP5(RX) ←───────  UART1 TX (GP16, optional debug console)
```

> Both `pico_enable_stdio_uart` and `pico_enable_stdio_usb` are set **0** in
> `CMakeLists.txt` — UART0 is reserved for the STM32 link and USB is reserved for
> future use. For `printf` during bring-up, wire a USB-serial adapter to GP16 (UART1
> TX) and set `pico_enable_stdio_uart(branch_wifi24 1)`. Do **not** enable stdio over
> USB.

### Deviations / design calls baked in (candidates for BC spec v1.1)

1. **True SPSC queues, no spinlocks.** Spec §4.1 says "lock-free SPSC… protected by
   hardware spinlocks," which is self-contradictory. Implementation uses a classic
   SPSC ring: producer writes `w_idx` only, consumer writes `r_idx` only, 32-bit
   aligned loads are atomic on M0+, with `__dmb()` barriers between element store and
   index update. Faster and correct. Flag §4.1 for clarification.
2. **PIO0 = RX, PIO1 = TX.** Spec §9 file comments implied both RX and TX in PIO0; the
   four RX state machines went into PIO0 and the TX state machine into PIO1 to resolve
   instruction-memory and FIFO contention. Resolves a spec ambiguity.
3. **Tombstone-based dedup eviction** (`dedup.c`) instead of rehash-on-eviction, which
   had a double-increment bug. Stale entries are tombstoned and reclaimed, not rehashed.
4. **`$RC` relay validates the embedded command's checksum** before forwarding
   (spec §8.2 requires it). Needed its own helper because the outer line's checksum is
   already consumed by the line receiver.

### Not implemented (spec open items — do not block bring-up)

- STM32-side `$TM` generation after each PPS edge.
- STM32-side `$RC` relay handling.
- SD-card ingest format.
- 1PPS signal-integrity test on a 5-Branch fan-out.
- Power-supply sizing for RP2040 + 4× ESP32-C3.

---

## 3. Everything else — not yet written

| Component | Status |
|---|---|
| STM32 firmware (#1 and #2) | Not started. Needs `$TM` gen, `$RC` relay, Branch UART parse, SD write, USB-CDC streaming. |
| Env Sensor Branch firmware (RP2040) | Specced (`system_plan_v2_1_amendment.md` §3.8), not written. Sensor set updated: ICM-42688-P, LIS3MDL, BMP390, SCD41, SGP41 (SHT40 dropped). |
| Other Branch Leaf/BC firmware | Not started: 5 GHz WiFi, BLE/BT, 802.15.4, Meshtastic, VHF/UHF ISM, FPV. |
| Analyzer (Root) | Not started against MKII. Plan: fork SSA, gut GNSS schema/routes, port `analysis_engine.py` + `data_sources.py` from Wardriving Analyzer. |
| Jetson Trunk software | Not started: DB writer, USB-CDC ingest, rtl_433 sidecar + GPS-tag wrapper + ingest adapter, trunk-recorder, Whisper pipeline. |

The SSA (v5.12.0) and Wardriving Analyzer (v4.8.0) codebases are external prior work,
not part of MKII's repo, referenced only as the analyzer's starting point.

---

## 4. Recommended first action for the code side

1. Stand up a git repo for MKII firmware.
2. Pull `leaf_wifi24.zip` and `branch_wifi24.zip` from the source chat and commit them
   verbatim as the baseline (they're known-good; capture them before iterating).
3. Reconcile the version/consistency issues in `HANDOFF.md` §9 (STM32 board, sensor
   set) before writing STM32 firmware or regenerating pinouts.
4. Then proceed down the TODO in `HANDOFF.md` §8.
