# STM32 Mid-Tier Aggregator Firmware Guide

**Version:** 1.0.0
**Date:** 2026-05-29
**Status:** New spec — supersedes the abandoned FK743M2-IIT6 design
**Target Hardware:** NUCLEO-H753ZI (STM32H753ZIT6, 144-pin)
**Scope:** Firmware for both STM32 units — STM32 #1 (RF Collection) and STM32 #2 (Sub-GHz/FPV). A single source tree builds both unit images via a compile-time `MKII_STM32_UNIT={1,2}` flag.

This document is the STM32-side analog of `branch_controller_wifi24_v1_0.md`. It specifies the firmware responsible for the mid-tier role: timing distribution, Branch UART aggregation, USB-CDC streaming to the Trunk, SD-card logging in standalone mode, and downstream command routing.

---

## 1. Hardware Interface Summary

### 1.1 Common to Both Units

| Function | Peripheral | Pins | Notes |
|---|---|---|---|
| GPS (NMEA + UBX) | I2C1 | PB6 SCL, PB7 SDA | M10Q-5883 (STM32 #1) or M10 basic (STM32 #2). 400 kHz fast-mode. |
| GPS 1PPS input | EXTI on GPIO | PG10 (EXTI10) | Rising-edge ISR; captures TIM2 free-running timestamp |
| ST-LINK VCP (debug console) | USART3 | PD8 TX, PD9 RX | Reserved for ST-LINK on NUCLEO-H753ZI; do not reassign |
| USB CDC to Trunk | USB_OTG_FS | PA11 DM, PA12 DP, PA9 VBUS sense | USER USB-C / micro-B connector (CN13 or board variant equivalent) |
| SD card (SDMMC 4-bit) | SDMMC1 | PC8–PC12 + PD2 CMD | External micro-SD breakout on ST Morpho header; SDIO 4-bit |
| User LED status | GPIO | PB0 (green), PE1 (yellow), PB14 (red) | Boot/fault/sync indicators |
| User button (mode override) | GPIO | PC13 (B1, BLUE) | Optional: force standalone mode on boot if held |

Notes:
- ETH on NUCLEO-H753ZI (LAN8742A on PA1/PA2/PA7/etc.) is **not used**. The Ethernet PHY is left disabled in clock config; its pins are not driven by firmware.
- Onboard ST-LINK V3 provides USART3 over USB as a debug console — keep it active in both unit images.

### 1.2 STM32 #1 — RF Collection

| Branch | UART | TX pin | RX pin | Notes |
|---|---|---|---|---|
| 2.4 GHz WiFi (W24) | USART1 | PA9 | PA10 | RP2040 BC link |
| 5 GHz WiFi (W5G) | USART2 | PD5 | PD6 | RP2040 BC link |
| BLE/BT (BLE) | UART4 | PB9 | PB8 | RP2040 BC link |
| 802.15.4 (DOT154) | UART5 | PB13 | PB12 | RP2040 BC link |
| Env Sensors (SEN) | USART6 | PC6 | PC7 | RP2040 BC link |
| (debug) | USART3 | PD8 | PD9 | ST-LINK VCP |
| (spare) | UART7 | PE8 | PE7 | Reserved |

5 Branch UARTs in use. No spare Branch UART after Env Sensor is added per `system_plan_v2_1_amendment.md` §3.1.

### 1.3 STM32 #2 — Sub-GHz / FPV

| Branch | UART | TX pin | RX pin | Notes |
|---|---|---|---|---|
| Meshtastic/Meshcore (MTC) | USART1 | PA9 | PA10 | RP2040 BC link |
| VHF ISM (VHF) | USART2 | PD5 | PD6 | RP2040 BC link |
| UHF ISM (UHF) | UART4 | PB9 | PB8 | RP2040 BC link |
| FPV Detection (FPV) | UART5 | PB13 | PB12 | RP2040 BC link |
| (spare) | USART6 | PC6 | PC7 | Reserved for future Branch |
| (debug) | USART3 | PD8 | PD9 | ST-LINK VCP |
| (spare) | UART7 | PE8 | PE7 | Reserved |

4 Branch UARTs in use; 2 spares retained.

### 1.4 PPS Fan-Out (Hardware Note, Informative)

The GPS PPS is a single CMOS push-pull signal. It is wired to:
- The STM32's PPS input (PG10 EXTI).
- Every attached Branch Controller's PPS input GPIO (one wire per Branch).

| Unit | Total PPS sinks | Buffer required |
|---|---|---|
| STM32 #1 | 1 (STM32) + 5 (Branches) = 6 | **Yes** — single `SN74LVC1G17DBVR` Schmitt-trigger buffer in front of the fan-out |
| STM32 #2 | 1 (STM32) + 4 (Branches) = 5 | Marginal at 5 sinks; the M10 datasheet rates the PPS drive at ~5 mA which is borderline. Buffer **recommended** but not strictly required. |

Pin assignments are preliminary — subject to final PCB layout. The firmware is GPIO-agnostic apart from the EXTI line number (PG10 → EXTI10).

---

## 2. Operating Modes

| Mode | USB CDC connected? | SD card present? | Behavior |
|---|---|---|---|
| **Standalone** | No | Required | All Branch records logged to SD; LED1 slow blink. No upstream stream. |
| **Connected** | Yes | Optional (used as backup) | Branch records streamed to USB CDC in real time; SD logs in parallel if mounted. LED1 solid on. |
| **Minimal** | Either | Either | Subset of Branches present (some UARTs absent). Operates with whatever responds. |

Mode is **detected at runtime**, not configured: presence of an active USB CDC host promotes to Connected; absence keeps Standalone. The user button held during boot forces Standalone even if USB is enumerated (useful for benchtop testing without polluting the Trunk DB).

---

## 3. Timing — 1PPS + `$TM` Emission

The STM32 is the source of `$TM` epoch messages. Each Branch Controller receives:
- the PPS edge directly on a GPIO (parallel with the STM32's own PPS pin), and
- a `$TM,epoch_s,fix_ok*XX\n` line on its Branch UART within ~100 ms of the edge.

### 3.1 PPS Capture

```c
// EXTI10 ISR on PG10 rising edge — minimal work
void EXTI15_10_IRQHandler(void) {
    if (EXTI->PR1 & (1 << 10)) {
        EXTI->PR1 = (1 << 10);
        pps_state.last_pps_tick = __HAL_TIM_GET_COUNTER(&htim2);  // TIM2 free-run @ 1 MHz
        pps_state.pps_count++;
        pps_state.pps_pending = true;
    }
}
```

TIM2 is configured as a free-running 32-bit up-counter at 1 MHz (prescaler from 100 MHz APB1 timer clock). `last_pps_tick` is the µs-resolution local timestamp of the edge.

### 3.2 GPS UTC Association

The STM32 reads NMEA `$GxRMC` (or UBX-NAV-TIMEUTC for sub-second-fresh timing) from the M10Q-5883 / M10 basic over I2C1.

Once a fix is acquired:
- `pps_state.epoch_s = <UTC seconds parsed from RMC>`
- The convention: RMC reports the UTC of the *most recent* PPS edge (the GPS already has this internally; the trick is reading it quickly enough). Practical observation across u-blox M10 family: RMC for the PPS at second N arrives within 50–80 ms of the edge.

The firmware reads the GPS at 10 Hz and looks for a fresh RMC after each PPS. When found, `epoch_s` is updated and `time_valid` is asserted. Subsequent reads that don't change second-of-day are ignored.

### 3.3 `$TM` Emission

After each PPS edge (in the main loop, not the ISR):

```c
if (pps_state.pps_pending && pps_state.time_valid) {
    char line[40];
    int len = format_tm_line(line, pps_state.epoch_s, pps_state.fix_ok);
    for (int b = 0; b < N_BRANCH_UARTS; b++) {
        branch_uart_tx_blocking(b, line, len);  // ~9 ms for 30 bytes at 230400 baud
    }
    pps_state.pps_pending = false;
}
```

Wire format: `$TM,epoch_s,fix_ok*XX\n` (see BC spec §3.2). On 5 Branches at 230 400 baud, total emit time is ~45 ms — comfortably within the 100 ms PPS-to-`$TM` budget. The transmission is sequential, not DMA, because the per-line burst is short and Branch UART TX is otherwise idle.

**If `time_valid == false`**: do not emit `$TM`. The Branches' PPS-only path keeps free-running with their last known epoch (BC spec §3.6). They will pick up timing again as soon as `$TM` resumes.

### 3.4 PPS Loss Detection

If no PPS edge for >2 seconds, mark `time_valid = false`, surface the condition on the heartbeat status, and start the BC-style free-running fallback. The STM32 keeps emitting any new `$TM` it can produce from GPS-only NMEA timing once the fix is back, even before a clean PPS edge — flagged with `fix_ok=0` until PPS resyncs.

LED2 (yellow) is driven on when `time_valid == false` to give a visible "timing degraded" indicator at the panel.

---

## 4. GPS Interface

### 4.1 I2C Read Path

The u-blox M10Q-5883 and the M10 basic both expose an I2C "DDC" interface that returns NMEA and UBX bytes when read. The address is `0x42 << 1` (`0x84` write / `0x85` read). The protocol:

1. Read register `0xFD-0xFE` (2 bytes, big-endian) — number of bytes available.
2. Read register `0xFF` — streaming read of that many bytes.

Poll at 10 Hz. The GPS produces ~100–200 bytes/s at 1 Hz solution rate (default) and up to ~2 KB/s at 10 Hz. The read latency dominates; the I2C bus is otherwise idle.

### 4.2 NMEA + UBX Parsing

| Source | Used for |
|---|---|
| NMEA `$GNRMC` / `$GPRMC` | UTC second-of-day → `epoch_s`; fix valid flag; lat/lon for `$PR` (position report) |
| NMEA `$GNGGA` / `$GPGGA` | Fix quality; satellite count; altitude (for SD log) |
| UBX `NAV-PVT` (optional) | Single binary message with epoch, lat, lon, fix, sat count — preferred over multi-NMEA when bandwidth matters |

The parser handles both NMEA framing (`$..*XX\r\n`) and UBX framing (`0xB5 0x62 <class> <id> <len_LE> <payload> <ck_a> <ck_b>`). UBX is preferred when present because it provides nanosecond-of-second from the GPS, which lets us cross-check the PPS edge alignment.

### 4.3 Position State

```c
struct GpsState {
    uint32_t epoch_s;
    bool     time_valid;     // PPS-synced epoch
    bool     fix_ok;         // 2D or 3D fix per RMC valid flag
    int32_t  lat_e7;         // latitude × 1e7
    int32_t  lon_e7;         // longitude × 1e7
    int16_t  alt_cm;         // altitude × 100 (cm)
    uint8_t  sat_count;
    uint8_t  hdop_x10;       // HDOP × 10
};
```

Updated by the GPS reader task. Snapshotted into each upstream record as the record passes through the STM32 forwarding layer.

### 4.4 GPS Configuration

On boot, configure the GPS via UBX-CFG-MSG / UBX-CFG-RATE to:
- 10 Hz solution rate
- PPS pulse: 1 Hz, 100 ms width, rising edge aligned to UTC
- Enable: `NAV-PVT`, `RMC`, `GGA`
- Disable: `GSV`, `GSA`, `VTG`, `GLL`, `GNS` (reduce bus traffic)
- Save to BBR + Flash so next boot is faster

The boot configuration is sent as a fixed sequence of UBX-CFG frames on I2C; failures are logged but non-fatal (the GPS will run with its previous saved config).

---

## 5. Branch UART Subsystem

### 5.1 RX — DMA Circular Buffers

Each Branch UART RX uses a dedicated DMA channel in **circular mode** writing into a 1 KB per-Branch ring buffer. DMA is started once at boot and never stopped. The firmware reads from the buffer using a software tail pointer compared against the DMA's `NDTR` (transfer count remaining).

| Symbol | Value | Notes |
|---|---|---|
| `BRANCH_RX_RING_SZ` | 1024 bytes | Per Branch UART |
| Total RX rings | 5 KB (#1) / 4 KB (#2) | |
| Max sustained line rate per Branch | ~115 lines/s at 200 B avg | At 230400 baud |

DMA is preferred over IDLE-line interrupt for two reasons: zero per-byte ISR overhead, and the H7's DMA1/DMA2 controllers each have 8 streams (enough for 5 USART RX + 5 USART TX + I2C1 RX/TX + SDMMC streams).

### 5.2 Line Assembly

Same NMEA-style framing as the Leaf/BC protocols (`$..*XX\n`, max 200 bytes). Each Branch has its own line-assembler state:

```c
struct BranchRxState {
    uint8_t  line_buf[200];
    uint16_t line_pos;
    bool     in_frame;
    uint32_t rx_lines_ok;
    uint32_t rx_lines_bad;     // checksum or length failures
    uint32_t rx_dma_overflows;
};
```

The assembler is identical to the Leaf and BC implementations — single-source it via a shared `proto.c` module compiled into both targets. Once a `\n` terminates a line and checksum verifies, the line is dispatched.

### 5.3 Dispatch

| Prefix | Action |
|---|---|
| `$WA`, `$WP`, `$ET`, `$DF`, `$BS` (W24) | Forward to USB CDC and SD log |
| `$EN`, `$SB` (SEN) | Forward to USB CDC and SD log |
| `$WA`-equivalents from other Branches (specs pending) | Forward to USB CDC and SD log |
| Any unknown `$XX` | Increment `rx_unknown` counter; log to debug UART; drop |

The STM32 does **not** parse the record fields beyond the message type — payloads are passed through verbatim with the per-record timestamp the BC already supplied. This minimizes firmware coupling to per-Branch protocol changes; only message-type prefixes are recognized centrally, and the field schema lives with each Branch's BC spec.

### 5.4 TX — Polled, Blocking

Branch TX is used only for:
- `$TM` on every PPS edge (sequential across Branches, ~9 ms per Branch).
- `$RC` relay forwarding when routed downstream (rare).

Polled HAL TX is sufficient. DMA TX would shave ~45 ms off the worst case per second but adds complexity for no useful benefit at this data rate.

---

## 6. USB CDC Interface

### 6.1 Endpoint Configuration

- USB FS device class via `USB_OTG_FS`.
- Single CDC interface, ACM model: bulk IN (62-byte EP0 size optional), bulk OUT.
- Vendor: 0x1209 (pid.codes Open Source); Product: 0xMKII (TBD assigned). Manufacturer string `"MKII"`, Product string `"MKII STM32 Aggregator"`.
- Per-unit Serial Number string includes the build-time `MKII_STM32_UNIT` value so the Trunk can disambiguate the two CDC devices it sees (`MKII-1` vs `MKII-2`).

### 6.2 Throughput Budget

Worst case per second, STM32 #1 fully populated:
- `$WA` from W24 at ~50/s × 90 B = 4.5 KB/s
- `$WP` from W24 at ~30/s × 60 B = 1.8 KB/s
- `$EN` from SEN at 10/s × 140 B = 1.4 KB/s
- `$BS` once per Branch per 10 s = negligible
- `$WA`-equivalents from W5G/BLE/DOT154 (future) = ~5 KB/s

Total ~15 KB/s sustained, well under USB FS bulk endpoint capacity (~64 KB/s practical). The CDC TX path uses a 4 KB ring buffer with auto-ZLP framing on natural line boundaries.

### 6.3 RX Path (Trunk → STM32)

USB CDC OUT is parsed with the same `$..*XX\n` line assembler. Recognized lines:

| Prefix | Action |
|---|---|
| `$RC` (relay command) | Route to the target Branch UART (see §7) |
| `$RQ,<branch>` (request branch status) | Forward to the named Branch UART; the BC's response (`$BS`) flows back via the normal RX path |
| `$CMD,...` (future host commands) | Reserved; ignored in v1.0.0 |

---

## 7. Downstream Command Routing — `$RC`

The Trunk sends `$RC,<leaf_id>,<inner_cmd>` for end-to-end Trunk-to-Leaf relay. The STM32 inspects only the `leaf_id` prefix to choose a Branch UART:

| `leaf_id` prefix | Branch UART |
|---|---|
| `W1`–`W4` | W24 BC (USART1 on STM32 #1) |
| `W5_1`–`W5_3` | W5G BC (USART2) |
| `BLE_*` | BLE BC (UART4) |
| `H2_*` | DOT154 BC (UART5) |
| `SEN` | SEN BC (USART6 on STM32 #1) |
| `MESH_*`, `MCORE_*` | MTC BC (USART1 on STM32 #2) |
| `VHF_*` | VHF BC (USART2 on STM32 #2) |
| `UHF_*` | UHF BC (UART4 on STM32 #2) |
| `FPV_*` | FPV BC (UART5 on STM32 #2) |
| (unknown) | Drop, increment `relay_unknown_leaf`, log to debug |

The STM32 does **not** validate the inner command's checksum — that responsibility is the BC's (per BC spec §8.2 + v1.1 amendment). The STM32 acts as a layer-3-style router on `leaf_id`.

The STM32 forwards the entire received `$RC,...` line verbatim onto the target Branch UART. The BC then re-validates and forwards the inner command to the Leaf.

---

## 8. SD Card Logging

### 8.1 File Layout

```
/MKII/<unit_id>/<session_id>/
    branch.log         # all Branch upstream records, one per line, gzip-rotated daily
    gps.log            # 1 Hz GPS state snapshots
    events.log         # boot, mode change, fault, PPS loss/resume
    session.json       # session metadata (boot time, unit, firmware version)
```

`session_id` = `YYYYMMDD_HHMMSS` of the first valid GPS fix. Until the first fix, records go to a pre-fix bucket (`session_id = pending`) and are renamed/moved when the first fix lands.

### 8.2 Write Strategy

- FATFS over SDMMC1 in 4-bit mode.
- 4 KB write buffer per file, flushed every 1 second or on buffer full.
- Power-cut safety: every flush also calls `f_sync` on `branch.log`. Worst-case loss is 1 second of records on hard power-down.
- File-handle reuse: open once, write many. Re-open if `f_write` returns FR_DISK_ERR (SD card removed/reinserted hot).

### 8.3 Connected-Mode Behavior

In Connected mode, SD logging continues in parallel with USB-CDC streaming. The SD is the durable record; the CDC stream is the live one. If the Trunk crashes mid-session, the SD log is complete and recoverable.

### 8.4 SD Absent / Failure

If SD card is not detected at boot or `f_mount` fails:
- LED3 (red) solid on for 5 s, then resume normal operation.
- All log writes become no-ops; counters track the would-have-written byte count.
- In Standalone mode this is a hard error and the firmware halts in a fault loop (LED3 fast blink). In Connected mode it's tolerated.

---

## 9. Module Decomposition

```
stm32_aggregator/
├── CMakeLists.txt                  STM32CubeIDE-compatible CMake (or Makefile alt)
├── Core/
│   ├── Inc/
│   │   ├── main.h
│   │   ├── stm32_config.h          MKII_STM32_UNIT, pin map (#1 vs #2)
│   │   └── aggregator_defs.h       Shared constants, structs, enums
│   └── Src/
│       ├── main.c                  HAL init, peripheral bring-up, mode detect, state machine
│       ├── stm32h7xx_it.c          ISRs (EXTI for PPS, DMA, USART)
│       ├── stm32h7xx_hal_msp.c     HAL MSP init
│       └── system_stm32h7xx.c      Clock tree
├── Drivers/
│   ├── STM32H7xx_HAL_Driver/       Vendor HAL (CubeMX-generated)
│   ├── CMSIS/                      Vendor
│   └── BSP/                        Per-unit pin macros
├── Middlewares/
│   ├── ST/STM32_USB_Device_Library/    CDC class
│   └── FatFs/                          SD logging
├── App/
│   ├── proto.h/.c                  $..*XX\n framing + checksum (shared with BC)
│   ├── gps.h/.c                    I2C GPS read, NMEA + UBX parse, config push
│   ├── pps_time.h/.c               EXTI ISR, TimeState, epoch association
│   ├── tm_emit.h/.c                $TM formatter + per-Branch TX
│   ├── branch_uart.h/.c            5/4 UART DMA RX setup, line assemblers
│   ├── dispatch.h/.c               Line-prefix dispatch (forward to USB CDC + SD)
│   ├── usb_cdc_app.h/.c            CDC TX ring buffer, RX line assembly
│   ├── cmd_router.h/.c             $RC leaf_id → Branch UART mapping
│   ├── sd_log.h/.c                 FatFS session file management
│   ├── mode_detect.h/.c            Standalone vs Connected detection
│   ├── status_led.h/.c             LED1/2/3 patterns
│   └── faults.h/.c                 Fault accounting + soft-reboot policy
└── pio_unused/                     (no PIO on STM32)
```

`Drivers/STM32H7xx_HAL_Driver` is generated by STM32CubeMX and not edited by hand. All MKII-specific firmware lives under `App/`. `Core/` is the CubeMX scaffold with our `main.c` calling into `App/`.

---

## 10. State Machine

```
[POR]
   │
   ├── Clock tree (480 MHz CPU, 240 MHz AXI, 100 MHz APB)
   ├── Peripheral init: USART1/2/3/4/5/6/7, I2C1, SDMMC1, USB_OTG_FS, TIM2, EXTI10
   ├── FatFS mount; LED3 if SD missing
   ├── USB CDC enumerate (non-blocking; sets `usb_ready` when host attaches)
   │
   ↓
[GPS_BOOT] ──(GPS UBX-CFG-* configure pushed; await first NMEA byte)──→
   │                                                                     │ (no GPS bytes in 30 s → continue, log fault)
   ↓                                                                     ↓
[WAIT_FIX] ──(first valid RMC + fix_ok)──→ [PPS_SYNC]
   │
   │   meanwhile: Branch UART DMA RX is already armed; lines are buffered.
   │   $TM cannot be emitted yet → buffered Branch records carry time_flag=1
   │   when forwarded.
   │
   ↓
[PPS_SYNC] ──(PPS_TIMEOUT_US elapsed without edge)──→ [DEGRADED]
   │
   │   normal operation:
   │     - on each PPS edge, emit $TM on all Branch UARTs
   │     - on each line received on a Branch UART, dispatch
   │     - service USB CDC RX for $RC / $RQ
   │     - flush SD log every 1 s
   │     - emit aggregator heartbeat every 10 s
   │
   ↓ (never exits — runs until power off)

[DEGRADED] ──(PPS resumes + $TM emitted + Branches re-sync)──→ [PPS_SYNC]
```

The aggregator's own heartbeat is a `$AG` message emitted on USB CDC and SD log:

```
$AG,unit_id,uptime_s,mode,time_valid,fix_ok,sat_count,sd_ok,n_branches_ok,err_count*XX\n
```

| Field | Description |
|---|---|
| unit_id | `1` or `2` |
| mode | `0`=Standalone, `1`=Connected |
| time_valid | 1 if PPS-synced |
| fix_ok | 1 if GPS fix |
| sat_count | satellites in view |
| sd_ok | 1 if SD mounted and writes succeeding |
| n_branches_ok | count of Branches that issued `$BS` in the last 30 s |
| err_count | cumulative checksum/DMA/queue errors |

---

## 11. Memory & Bandwidth Budget

| Allocation | Size | Notes |
|---|---|---|
| Branch RX DMA rings (5×) | 5 KB | 1024 B each (STM32 #1; #2 uses 4) |
| Line assembly buffers (5×) | 1 KB | 200 B each |
| USB CDC TX ring | 4 KB | |
| USB CDC RX line buf | 256 B | |
| SD log write buffers (4×) | 16 KB | 4 KB each |
| GPS RX buffer | 4 KB | I2C read accumulator |
| Time + GPS state | 128 B | |
| FatFS work area | ~16 KB | |
| Stacks (MSP + PSP) | 12 KB | |
| Heap | 32 KB | Conservative; nothing heavy lives here |
| **Total** | **~90 KB** | Out of 1 MB SRAM on STM32H753 |

Headroom: ~900 KB free. Branch RX rings can grow 8–16× if bursty Branches are added.

Sustained USB CDC bandwidth target: ~30 KB/s peak (2 × the per-unit estimate in §6.2). USB FS bulk OUT handles this with ~50% headroom.

---

## 12. Build Configuration

### 12.1 Toolchain

`arm-none-eabi-gcc` 12.x or newer (`-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb`). CubeMX-generated linker script lives at `STM32H753ZITX_FLASH.ld`.

### 12.2 Build Flags

```cmake
target_compile_definitions(stm32_aggregator PRIVATE
    USE_HAL_DRIVER
    STM32H753xx
    MKII_FW_VERSION="1.0.1"                # firmware build; bumped for the 480 MHz clock-tree fix
    MKII_STM32_UNIT=${MKII_STM32_UNIT}     # 1 or 2, passed at configure time
)
```

Two artifacts per release: `stm32_aggregator_unit1.elf` and `stm32_aggregator_unit2.elf`. Pin maps and Branch tables in `stm32_config.h` switch on `MKII_STM32_UNIT`.

### 12.3 CI

GitHub Actions runs `cmake -DMKII_STM32_UNIT=1 ..` then `make`, repeated for unit 2, on every push to `main` and on PRs. Artifacts uploaded as workflow outputs.

---

## 13. Testing Procedure

### 13.1 Bench Test — STM32 + GPS Only

1. Wire M10Q-5883 to I2C1 (PB6/PB7) and PPS to PG10.
2. Flash unit 1 firmware. Open ST-LINK VCP at 115200.
3. Verify boot log shows `GPS_BOOT` → `WAIT_FIX` → `PPS_SYNC` once outdoors.
4. Verify `$AG` appears on debug UART every 10 s with `time_valid=1`, `fix_ok=1`.

### 13.2 Bench Test — STM32 + 1 Branch (W24)

1. Wire RP2040 BC (with WiFi 2.4 Branch firmware) to USART1.
2. Verify `$TM` is observed on USART1 TX with a scope or logic analyzer within 100 ms of each PPS rising edge.
3. Verify `$WA`/`$BS` arrive from the BC on USART1 RX.
4. Connect USB CDC to a host; verify the same `$WA`/`$BS` lines appear on the CDC stream.

### 13.3 Bench Test — Standalone Mode

1. Disconnect USB CDC.
2. Insert SD card.
3. Power-cycle.
4. After a 1-minute run, power down, mount SD card on a host, verify `/MKII/1/YYYYMMDD_HHMMSS/branch.log` is present and non-empty.

### 13.4 Integration Test — STM32 + All 5 Branches

1. Connect all 5 Branch BCs.
2. Verify `n_branches_ok=5` in `$AG` after 30 s.
3. Run for 1 hour; verify SD log size matches expected throughput (~50 MB ± 20%).
4. Pull USB cable mid-run; verify the firmware remains in Connected mode for ~5 s then degrades to Standalone, with SD logging unbroken across the transition.

### 13.5 PPS Loss / Recovery

1. With everything running, momentarily disconnect the PPS wire.
2. Verify `time_valid` clears within 2 s, LED2 turns on, downstream `time_flag` on Branch records goes to 1.
3. Reconnect PPS; verify `time_valid` reasserts within 1 s of next edge, LED2 clears.

---

## 14. Open Items

| # | Item | Status |
|---|---|---|
| 1 | Final PCB pin assignments (`stm32_config.h` is preliminary) | Preliminary |
| 2 | UBX-CFG sequence: exact byte lists for the M10Q-5883 vs M10 basic variants | **v1.0.x ships `gps_push_config()` as a no-op**; the module must be pre-configured via u-center (NMEA RMC+GGA @1 Hz on I2C; PPS 1 Hz / 100 ms / rising-edge UTC). Sending `CFG-VALSET` at boot is targeted for **v1.1**. See firmware `README.md` §GPS pre-configuration. |
| 3 | SDMMC1 + breakout: confirm pin compatibility on ST Morpho header for the chosen SD breakout | Hardware bring-up |
| 4 | Power: 5V/3.3V draw with 5 Branches attached and USB host attached | Not yet analyzed |
| 5 | Firmware update path: USB DFU vs ST-LINK reflash policy | TBD; ST-LINK for v1.0 |
| 6 | Watchdog: IWDG window + soft-reset on stuck state machine | Implementation decision (recommended IWDG @ 8 s) |
| 7 | Branch-side `$RC` echo / confirmation protocol | None planned; the Trunk infers success from subsequent `$BS` |
| 8 | Compass on M10Q-5883 (QMC5883L integrated) | Not used in v1.0; Env Sensor Branch's LIS3MDL is authoritative |

---

## 15. Cross-References

- `system_plan_v2.md` + `system_plan_v2_1_amendment.md` + `system_plan_v2_2_amendment.md` — architecture, Branch inventory, schema.
- `wifi24_leaf_protocol_v1_1.md` + `wifi24_leaf_protocol_v1_2_amendment.md` — Leaf protocol.
- `branch_controller_wifi24_v1_0.md` + `branch_controller_wifi24_v1_1_amendment.md` — RP2040 BC firmware guide.
- HANDOFF.md §9 item 1 — STM32 board change FK743 → NUCLEO-H753ZI (this spec is the resolution).
