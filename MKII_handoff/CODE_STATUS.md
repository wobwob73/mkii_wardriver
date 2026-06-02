# MKII — Code Status & Firmware Inventory

**Compiled:** 2026-06-02
**Scope:** Every firmware tree produced for MKII, where it lives in this repo, how it
builds, its version, and the deliberate, documented deviations from the spec baked into
each. All six trees are **in this repository** under `firmware/`; build status rests on
the per-tree GitHub Actions workflows in `.github/workflows/`, not on any prior manual
confirmation.

> **History note.** `leaf_wifi24` and `branch_wifi24` were originally written in a prior
> session and lost when that sandbox was wiped. They have been **re-derived from the
> specs** (`wifi24_leaf_protocol_v1_1.md` + v1.2 amendment, and
> `branch_controller_wifi24_v1_0.md` + v1.1 amendment) and version-bumped to fold those
> amendments in. They are no longer "missing / retrieve from chat" — they are committed
> here, and their correctness now rests on CI compilation plus bench bring-up, not the
> earlier VSCode/PlatformIO observation.

---

## Inventory at a glance

| Tree | Target | Framework / build | Implements | FW version | Build status |
|---|---|---|---|---|---|
| `leaf_wifi24` | ESP32-C3 | PlatformIO / Arduino | `wifi24_leaf_protocol_v1_1` + v1.2 + **v1.3 (Scan-Hop)** amendments | `1.3.0` | CI (`leaf_wifi24.yml`) |
| `branch_wifi24` | RP2040 | pico-sdk + CMake → `.uf2` | `branch_controller_wifi24_v1_0` + v1.1/v1.2 amendments | `1.2.0` | CI (`branch_wifi24.yml`) |
| `leaf_wifi5` | ESP32-C5 | PlatformIO / **pioarduino** fork | `wifi5_branch_v1_0` §3–§8 | `1.0.2` | CI (`leaf_wifi5.yml`) |
| `branch_wifi5` | RP2040 | pico-sdk + CMake → `.uf2` | `wifi5_branch_v1_0` (BC side) + BC v1.2 amendment | `1.1.0` | CI (`branch_wifi5.yml`) |
| `leaf_ble` | ESP32-S3 | PlatformIO / ESP-IDF (NimBLE) | `blebt_branch_v1_0` §7 (BLE Leaf) | `1.0.0` | CI (`leaf_ble.yml`) |
| `env_sensor_branch` | RP2040 | pico-sdk + CMake → `.uf2` | `env_sensor_branch_v1_0` | `1.0.1` | CI (`env_sensor_branch.yml`) |
| `stm32_aggregator` | NUCLEO-H753ZI ×2 | bare-metal HAL (CMake + FetchContent CubeH7) + host-smoke | `stm32_h753_firmware_v1_0` | `1.0.2` | CI (`stm32_aggregator.yml`): host-smoke ×2 + on-target ×2 |
| `agg_lite` | RP2040 | pico-sdk + CMake → `.uf2` | `lite_aggregator_v1_0` | `1.0.0` | CI (`agg_lite.yml`) |

Version strings live in each tree's `*_defs.h` and build flag, and are mirrored in the
matching spec's build-flag example. Spec **document** versions (the `**Version:**` header
in each `specs/*.md`) are a separate namespace from firmware build versions.

---

## 1. `leaf_wifi24` — 2.4 GHz WiFi Leaf firmware

| | |
|---|---|
| **Target** | ESP32-C3 (`seeed_xiao_esp32c3`) |
| **Framework** | Arduino via PlatformIO |
| **Implements** | `wifi24_leaf_protocol_v1_1.md` **with the v1.2 and v1.3 (Scan-Hop) amendments applied** |
| **Version** | `1.3.0` (`LEAF_VERSION` in `platformio.ini` + fallback in `include/leaf_defs.h`) |
| **Status** | Re-derivation from spec + v1.3 Scan-Hop; builds in CI |

Single binary for all WiFi 2.4 GHz Leaf roles. Identity adopted from the first
`$CF` on the BC UART link. W1/W2/W3 = passive scan (channels 1/6/11); W4 = WIDS
(promiscuous, channel hopping); **WH1 = Scan-Hop** (`mode=2`, v1.3): a single Leaf
round-robins a channel set (default 1057 = 1/6/11, 200 ms/ch), emitting one `$BK`
per full sweep — added for the light-duty single-box variant (`agg_lite`), which
covers 2.4 GHz with one Leaf. The three earlier deviations — extended encryption
enum (OWE, WPA3-Enterprise), passive scan timing (~200 ms/ch), and WIDS ring
**drop-newest** on overflow — are spec-conformant (folded into the v1.2 amendment).

**v1.3 (this pass):** added `LEAF_MODE_SCANHOP`, `$CF mode=2`, `$CH` now applies to
Scan-Hop as well as WIDS, the `WH1` leaf-id convention, and a Scan-Hop scan driver
(`wifi_scan.cpp`: hop-set from mask, per-sweep `$BK`). Per §5.2, the **firmware-wide
standalone fallback is now Scan-Hop** (mask 1057, 200 ms) — but in the full Branch the
BC always sends an explicit `$CF` first, so fielded W1–W4 behavior is unchanged. See
`firmware/leaf_wifi24/README.md` for the per-file layout and the full deviation log.

**Build:** `pio run -e leaf_wifi24` (`-t upload` to flash).

---

## 2. `branch_wifi24` — 2.4 GHz WiFi Branch Controller firmware

| | |
|---|---|
| **Target** | RP2040 |
| **Framework** | pico-sdk + CMake (needs `multicore`, PIO, DMA chaining) |
| **Implements** | `branch_controller_wifi24_v1_0.md` + v1.1 amendment |
| **Version** | `BC_FW_VERSION` `1.1.0` (`include/branch_defs.h`) |
| **Status** | Re-derivation from spec; builds to `.uf2` in CI |

Dual-core. Core 0: 4× PIO UART Leaf links with self-chaining DMA ring buffers, line
assembly, dispatch, Leaf health watchdog. Core 1: 1PPS ISR + `$TM` timestamping, AP
dedup (500 ms windows, tombstone eviction), WIDS analysis (evil-twin + deauth-flood),
upstream TX to STM32 #1 USART1. True SPSC queues (no spinlocks), PIO0=RX / PIO1=TX, and
`$RC` checksum validation are design calls reflected in the v1.1 amendment. See
`firmware/branch_wifi24/README.md` for layout, SWD bring-up notes, and the deviation log.

**Build:** `mkdir build && cd build && cmake -DPICO_BOARD=pico .. && make -j` → `branch_wifi24.uf2`.

---

## 3. `leaf_wifi5` — 5 GHz WiFi Leaf firmware

| | |
|---|---|
| **Target** | ESP32-C5 (`esp32-c5-devkitc-1`) |
| **Framework** | Arduino via the **pioarduino** platform fork (mainline `espressif32` lacks the C5 board file + arduino-esp32 v3.x) |
| **Implements** | `wifi5_branch_v1_0.md` §3–§8 (Leaf side) |
| **Version** | `1.0.1` (`LEAF_VERSION`) — bumped from `1.0.0` for the band-select fix below |
| **Status** | Builds in CI |

Single binary for the 2–3 ESP32-C5 Leaves. W5_1/W5_2 scan, W5_3 WIDS, across the 5 GHz
channel sets.

**Fix in 1.0.1 (this pass):** the boot-time `esp_wifi_set_band(WIFI_BAND_5G)` call in both
`wifi_scan::init()` and `wids_monitor::start()` was guarded by `#ifdef WIFI_BAND_5G`.
`WIFI_BAND_5G` is an esp-idf `wifi_band_t` **enumerator**, not a preprocessor macro, so
the guard was always false and the call was compiled out — the C5 never left the 2.4 GHz
band. The guard now references `#ifdef LEAF_BAND_5G` (the build flag from `platformio.ini`).
The enumerator is in scope via `<esp_wifi.h>`/`<esp_wifi_types.h>` and is present in the
arduino-esp32 v3.x / IDF 5.4 the pinned pioarduino platform carries.

**Build:** `pio run -e leaf_wifi5`. The pioarduino **platform resolves** and the board is
recognized; the arduino-esp32 framework core is fetched from GitHub release assets on first
build, which the unrestricted CI runner does but a TLS-intercepting sandbox may block
(self-signed proxy CA rejected by PlatformIO's downloader). The band-select fix itself is a
one-line guard change; full compilation is exercised by `leaf_wifi5.yml` in CI.

---

## 4. `branch_wifi5` — 5 GHz WiFi Branch Controller firmware

| | |
|---|---|
| **Target** | RP2040 |
| **Framework** | pico-sdk + CMake |
| **Implements** | `wifi5_branch_v1_0.md` (BC side) |
| **Version** | `BC_FW_VERSION` `1.0.0` (`include/branch_defs.h`) |
| **Status** | Builds to `.uf2` in CI |

Same dual-core architecture as `branch_wifi24`, aggregating 2–3 ESP32-C5 Leaves
(`W5_1`/`W5_2`/`W5_3`), Branch ID `W5G`, upstream to STM32 #1 USART2. See
`firmware/branch_wifi5/README.md`.

**Build:** `mkdir build && cd build && cmake -DPICO_BOARD=pico .. && make -j` → `branch_wifi5.uf2`.

---

## 5. `env_sensor_branch` — Environmental Sensor Branch firmware

| | |
|---|---|
| **Target** | RP2040 (single board, no Leaf tier) |
| **Framework** | pico-sdk + CMake |
| **Implements** | `env_sensor_branch_v1_0.md` + `system_plan_v2_2_amendment.md` (`$EN`/`$SB`) |
| **Version** | `SENSOR_FW_VERSION` `1.0.0` (`include/env_defs.h`) |
| **Status** | Builds to `.uf2` in CI |

Direct I2C0 to ICM-42688-P (IMU), LIS3MDL (mag), BMP390 (baro), SCD41 (CO2/RH/temp),
SGP41 (VOC/NOx). Madgwick AHRS at 100 Hz decimated to 10 Hz `$EN`; `$SB` every 10 s.

**Open placeholder (decided this pass — keep stub, target v1.1):** the Sensirion **Gas
Index Algorithm is stubbed** in `src/sgp41.c`. The SGP41 raw signals are read and
CRC-checked, but `sgp41_run_gas_index()` returns the algorithm baseline (`voc=100`,
`nox=1`) once conditioned and `-1` otherwise. Therefore the `$EN` `voc_index`/`nox_index`
fields are **baseline-only constants** in v1.0.x — presence/conditioning indicators, not
graded readings. Recorded in `env_sensor_branch_v1_0.md` §14 item 9 with a **v1.1** target;
the upgrade path (vendoring the BSD algorithm) is in `firmware/env_sensor_branch/README.md`.
Rationale: integrating ~1000 lines of vendor code is out of scope for a stabilization pass
and cannot be validated without controlled gas exposure on hardware.

**Build:** `mkdir build && cd build && cmake -DPICO_BOARD=pico .. && make -j` → `env_sensor_branch.uf2`.

---

## 6. `stm32_aggregator` — STM32 mid-tier aggregator firmware

| | |
|---|---|
| **Target** | NUCLEO-H753ZI (STM32H753ZIT6) — one source tree, two unit images |
| **Framework** | Bare-metal STM32H7 HAL; CMake fetches STM32CubeH7 (HAL/USB/FatFS) via FetchContent |
| **Implements** | `stm32_h753_firmware_v1_0.md` |
| **Version** | `MKII_FW_VERSION` `1.0.1` (`Core/Inc/stm32_config.h` + CMake define) — bumped from `1.0.0` for the clock-tree fix below |
| **Status** | CI builds host-smoke (App/ + stub PAL) ×2 units **and** the on-target image ×2 units |

`App/` holds portable C business logic (framing, PPS/`$TM`, GPS NMEA, Branch UART pump,
USB CDC, `$RC` routing, SD logging, `$AG` heartbeat) driven through a Platform
Abstraction Layer. Two PAL backings exist: `Platform/Src/pal_hal.c` (stub, host-smoke
only) and `Platform/Src/pal_hal_stm32h7.c` (real HAL, on-target). `Core/Src/main.c`
hand-codes `SystemClock_Config()` + every `MX_*_Init()`; there is **no CubeMX `.ioc`** in
the repo (the equivalent state lives in `main.c` + `stm32h7xx_hal_msp.c`).

**Unit selection:**
- `MKII_STM32_UNIT=1` — RF Collection (WiFi24, W5G, BLE, DOT154, SEN)
- `MKII_STM32_UNIT=2` — Sub-GHz / FPV (MTC, VHF, UHF, FPV)

**Fix in 1.0.1 (this pass) — coherent 480 MHz clock tree.** `SystemClock_Config` set
`PLLN=240` with `PLLP=2`, yielding a 480 MHz VCO but only **240 MHz SYSCLK**, contradicting
the header comment and the VOS0 scaling (which is required only above 400 MHz). Decision:
achieve the intended coherent tree. `PLLN` is now `480` → 960 MHz VCO → **480 MHz SYSCLK /
240 MHz AXI / 120 MHz APB**. The two clock-dependent constants were recomputed to match:
- **I2C1 `TIMINGR`** `0x10C0ECFF` (≈100 kHz, and mislabeled "400 kHz @ 100 MHz") →
  `0x5320131D`, a genuine 400 kHz at the corrected 120 MHz PCLK1 (derivation in `main.c`).
- **TIM2 prescaler** `99` → `239`: TIM2's APB1 timer clock is 2×PCLK1 = 240 MHz, so 239
  gives the 1 MHz tick that `pal_time_us_64()` depends on.

These are correct-by-construction; SCL frequency and PPS timing still need a logic-analyzer
check at bench bring-up (`stm32_h753_firmware_v1_0.md` §13).

**GPS pre-config placeholder (decided this pass — keep no-op, target v1.1):**
`App/gps.c::gps_push_config()` is a no-op; the M10 module must be pre-configured once via
u-center (NMEA RMC+GGA @1 Hz on I2C; PPS 1 Hz / 100 ms / rising-edge UTC). Documented in
`firmware/stm32_aggregator/README.md` §GPS pre-configuration and recorded in
`stm32_h753_firmware_v1_0.md` §14 item 2 with a v1.1 target. Rationale: blind UBX
`CFG-VALSET` at boot is unverifiable here and risks a worse failure than the documented
factory-default + BBR path the code already relies on.

**Build (host-smoke):** `cmake -DMKII_STM32_UNIT=1 .. && make`
**Build (on-target):** `cmake -DMKII_STM32_TARGET=on -DMKII_STM32_UNIT=1 -DCMAKE_TOOLCHAIN_FILE=../Platform/arm-none-eabi.cmake .. && ninja` → `stm32_aggregator_unit1.{elf,bin,hex}` (~82 KB).

---

## 6a. `leaf_ble` — BLE-only Leaf firmware (NEW)

| | |
|---|---|
| **Target** | ESP32-S3 (`esp32-s3-devkitc-1`) |
| **Framework** | ESP-IDF via PlatformIO (NimBLE host) |
| **Implements** | `blebt_branch_v1_0.md` §7 (BLE Leaf side) |
| **Version** | `1.0.0` (`LEAF_VERSION` in `platformio.ini` + fallback in `src/ble_defs.h`) |
| **Status** | First build of this Leaf; compiles in CI (`leaf_ble.yml`) |

NimBLE passive observer: extended scan across all three primary adv channels on
1M + Coded PHYs, per-event TLV parse (flags, name, 16-bit UUIDs, Manufacturer Specific
Data → Company ID), intra-Leaf 1 s dedup, emits `$BL`/`$BX`/`$BK`/`$HB`. Identity from
`$CF` (`BLE-1`). Strict listen-only (`passive=1`, no scan requests/connections). The host
callback only enqueues; the main loop drains and emits (§7.3 critical rule). BT Classic
(`leaf_bt_classic`) is a separate binary and is **not** built here (light-duty defers it).

**Documented limitations** (in `firmware/leaf_ble/README.md`, not silent): `$BL.channel`
is `0` (NimBLE host does not surface the per-report primary channel); `$CF`
`window_ms`/`interval_ms` are advisory under the fixed 100%-duty observer (§7.3); Coded
PHY S=2/S=8 not distinguished (`phy=3`); RPAs classified but not resolved (analyzer task).
On-hardware NimBLE Coded-PHY stability is `blebt_branch_v1_0.md` §17 item 5.

**Build:** `pio run -e leaf_ble` (ESP-IDF framework fetched on first build, as `leaf_wifi5`).

---

## 6b. `agg_lite` — Light-Duty Single-Box Aggregator firmware (NEW)

| | |
|---|---|
| **Target** | RP2040 (single board, three Leaves, own GPS + SD) |
| **Framework** | pico-sdk + CMake → `.uf2` (multicore, PIO, DMA, SPI) |
| **Implements** | `lite_aggregator_v1_0.md` |
| **Version** | `AGG_LITE_FW_VERSION` `1.0.0` (`include/agg_defs.h` + CMake define + spec §12.2) |
| **Status** | First build; builds to `.uf2` in CI (`agg_lite.yml`) |

Collapses the Branch-Controller and STM32 mid-tier into one RP2040 for fixed-position
use. Core 0 runs three PIO UART Leaf links (W24 Scan-Hop `WH1`, W5G scan `W5_1`, BLE
`BLE-1`) with the BC's line assembly / dispatch / health watchdog. Core 1 owns the GPS
(UART1 NMEA, F-003 validation), 1PPS + **locally generated `$TM`** (F-004 atomic
snapshot), three dedup pipelines (WiFi ×2 re-entrant + BLE 5 s/1024), the upstream record
formatter (`$WA`/`$BD`/`$BX` — byte-identical to the multi-box schemas — plus the new
`$LA` consolidated heartbeat), and an SD writer draining a 32 KB RAM ring. Optional live
USB-CDC mirror. Reuses `proto`/`pio_uart`/`pps_time`/`leaf_health`/dedup substantively
unchanged from `branch_wifi24`; `gps`/`sd_log` ported from the STM32 `App/` layer.

**Not produced in light-duty v1.0** (by design): `$ET`/`$DF` (no WIDS Leaf), `$BC_T`
(no BT Classic), `$WP` (no promiscuous probe source) — `lite_aggregator_v1_0.md` §6/§7.

**SD backend is a documented PLACEHOLDER (decided this pass — keep stub, integrate later).**
`src/sd_spi_fatfs.c` is the seam to the vendored FatFs-over-SPI library that §9 lists as
`[NEW vendored]` and §14 item 5 flags as implementation-phase. The default build links a
placeholder backend (`AGG_SD_BACKEND=stub`) that brings up SPI0 + card-detect and accounts
writes but does **not** persist them, so `agg_lite.uf2` compiles in CI and the
ring/session/state-machine/`$LA` path runs on the CDC mirror. This is the same convention
as the STM32 PAL host-smoke stub, `gps_push_config()` no-op, and the SGP41 gas-index stub.
Durable logging = vendor the library into `third_party/pico_fatfs_spi/` and reconfigure
with `-DAGG_SD_BACKEND=fatfs` (a thin adapter; nothing above `sd_spi_fatfs.h` changes).
Also: GPS transport is UART1 not I2C (§1, open item 1); `gps_push_config()` is a no-op
(§5, open item 6). Full deviation log in `firmware/agg_lite/README.md`.

**Build:** `mkdir build && cd build && cmake -G Ninja -DPICO_BOARD=pico .. && ninja agg_lite` → `agg_lite.uf2`.

---

## 7. Not yet written

The firmware still to be built is the remaining **protocol branches** outside the current
scope, each comprising Leaf + Branch-Controller firmware. (The BLE Leaf is now built — see
§6a — though its RP2040 BC `branch_blebt` and the BT Classic Leaf remain.)

| Branch | STM32 unit / UART | Leaf MCU |
|---|---|---|
| BLE / BT (RP2040 BC `branch_blebt` + `leaf_bt_classic`; `leaf_ble` done) | #1 | ESP32-S3 |
| 802.15.4 (Thread/Zigbee/Matter) | #1 | ESP32-H2 |
| Meshtastic / Meshcore (LoRa) | #2 | LoRa modules |
| VHF ISM (315/433) | #2 | Arduino Nano + FSK RX |
| UHF ISM (868/915) | #2 | mixed Leaves |
| FPV detection | #2 | RX5808 + LoRa sniffers |

Also unbuilt (non-firmware): the **Analyzer (Root)** application (fork SSA, port
`analysis_engine.py` + `data_sources.py`) and the **Jetson Trunk** software (SQLite writer,
USB-CDC ingest, rtl_433 sidecar + GPS-tag wrapper, trunk-recorder, Whisper pipeline). The
SSA (v5.12.0) and Wardriving Analyzer (v4.8.0) codebases are external prior work, referenced
as the analyzer's starting point, not part of this repo.

---

## 7a. Review-driven hardening (2026-06-02)

An external code review (`mkii_code_analysis_report.md`, 2026-06-01) found a
set of correctness defects that CI cannot catch — they affect whether
hardware actually does what the source claims. All confirmed Critical / High
findings are addressed in this pass; the per-tree READMEs and
`branch_controller_wifi24_v1_2_amendment.md` describe the changes:

- **F-001 (Critical):** PIO TX retarget now updates `PINCTRL.OUT_BASE` /
  `SIDESET_BASE` and self-checks. Without this, W2..W4 / W5_2..W5_3 never
  received their `$CF`. **Hardware-only verification.**
- **F-002 (Critical):** `$RC` relay wire format is now `$RC,<target>,<hex>*XX`
  — the plaintext-nested form was unparseable. Implementation + spec in
  `branch_controller_wifi24_v1_2_amendment.md` §8.2.
- **F-003:** STM32 GPS now validates NMEA checksums + ranges before any
  timebase update.
- **F-004:** PPS state on all four trees now read via atomic snapshot
  accessor (RP2040: hardware spinlock; STM32: PRIMASK).
- **F-005:** STM32 `pal_time_us_64()` is now UIF-aware, closing the
  pre-ISR overflow window. **Hardware-only verification of behavior.**
- **F-006:** All proto validators now require `*XX` to be exactly the last
  three bytes; line receivers strip trailing CR. Cross-tree.
- **F-007 (partial):** WIDS correctness fix — `ET_ENC_MISMATCH` now emitted,
  single-radio alerts no longer fake a known-vs-rogue identity. Full
  SSID-keyed evil-twin detector deferred.
- **F-008:** Leaf `send_line()` is now bounded (chunked writes, 50 ms
  deadline, drop counter).

Deferred review items (with version targets) are in `HANDOFF.md` §9.

---

## 8. Recommended next actions (code side)

1. Bench bring-up of `stm32_aggregator` per `stm32_h753_firmware_v1_0.md` §13: flash both
   unit images, verify the 480 MHz clock, 400 kHz I2C SCL, and 1 MHz TIM2 tick on a scope,
   then GPS-only (`$AG`/`$TM`/PPS) with a u-center-preconfigured M10.
2. Bench bring-up of the 2.4 GHz and 5 GHz WiFi Branches (Leaf + BC) against their spec
   §13/§11 procedures; confirm the leaf_wifi5 band-select fix puts the C5 on 5 GHz.
3. Env Sensor Branch bring-up; treat `voc`/`nox` as baseline-only until the v1.1 gas-index
   integration.
4. Then the six unbuilt protocol branches, and the Analyzer / Trunk software.
