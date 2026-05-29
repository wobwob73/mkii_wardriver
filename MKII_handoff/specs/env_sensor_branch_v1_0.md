# Environmental Sensor Branch — RP2040 Firmware Guide

**Version:** 1.0.0
**Date:** 2026-05-29
**Scope:** RP2040 firmware for the Environmental Sensor Branch. Single-board Branch (no Leaves). Streams `$EN` at 10 Hz and `$SB` at 0.1 Hz on USART6 to STM32 #1.
**Target Hardware:** RP2040 + ICM-42688-P + LIS3MDL + BMP390 + SCD41 + SGP41
**Implements:** Sensor Branch protocol per `system_plan_v2_1_amendment.md` §3.8 + `system_plan_v2_2_amendment.md` (sensor set update)

This spec is the firmware-guide analog of `branch_controller_wifi24_v1_0.md` for the Env Sensor Branch. The Sensor Branch has no Leaves — all sensors hang directly off the RP2040's I2C0 bus.

---

## 1. Hardware Interface Summary

```
                        RP2040
                  ┌───────────────────┐
  I2C0 SDA ───←→─ │ GP4 (I2C0 SDA)    │  ─→ ICM-42688-P  (addr 0x68)
  I2C0 SCL ───→── │ GP5 (I2C0 SCL)    │  ─→ LIS3MDL      (addr 0x1C)
                  │                   │  ─→ BMP390       (addr 0x77)
                  │                   │  ─→ SCD41        (addr 0x62)
                  │                   │  ─→ SGP41        (addr 0x59)
                  │                   │
  STM32 TX ────→  │ GP13 (UART0 RX)   │
  STM32 RX ←────  │ GP12 (UART0 TX)   │
                  │                   │
  1PPS ─────────→ │ GP10 (GPIO EXTI)  │
  IMU INT ──────→ │ GP6  (data-ready) │  optional fast-path
  (debug) ←─────  │ GP16 (UART1 TX)   │  optional 115200 console
                  └───────────────────┘
```

| Interface | Peripheral | Baud/Config | Purpose |
|---|---|---|---|
| I2C sensor bus | I2C0 | 400 kHz fast-mode | All 5 sensors share one bus |
| STM32 upstream | UART0 | 230 400 8N1 | `$EN`/`$SB` upstream, `$TM` downstream |
| 1PPS input | GPIO10 IRQ | Rising edge | Sub-second time sync |
| IMU data-ready | GPIO6 IRQ | Falling edge | Optional INT1 from ICM-42688-P (low-latency fusion tick) |
| Debug UART | UART1 (optional) | 115 200 8N1 | Boot log + fault traces |

Pin assignments preliminary — match the WiFi24 BC's PPS/UART pins so the Env Sensor Branch can drop into any STM32 #1 Branch UART without firmware retargeting.

### 1.1 I2C Topology Notes

- All five sensors are on I2C0 with no addressing conflicts:

| Sensor | I2C addr (7-bit) | Datasheet notes |
|---|---|---|
| ICM-42688-P | 0x68 | AD0=GND; alternate 0x69 if AD0=VCC |
| LIS3MDL | 0x1C | SDO/SA1=GND; alternate 0x1E if SA1=VCC |
| BMP390 | 0x77 | SDO=VCC; alternate 0x76 if SDO=GND |
| SCD41 | 0x62 | Fixed |
| SGP41 | 0x59 | Fixed |

- Bus capacitance budget at 400 kHz: 5 devices on a short trace are well within the 400 pF I2C spec; standard 2.2 kΩ pullups to 3.3 V on SDA/SCL.
- Bus recovery: if a slave hangs SDA low (read interrupted by reset), the firmware clocks SCL 9 times to release the bus before re-init, per the standard I2C recovery procedure.

---

## 2. Dual-Core Architecture

### 2.1 Core Assignment

| Core | Responsibilities |
|---|---|
| **Core 0** | ICM-42688-P read at 100 Hz (via INT1 or timer). Madgwick AHRS fusion using accel + gyro + LIS3MDL mag. Maintains the fused quaternion + Euler angles. Decimates to 10 Hz and pushes orientation + raw accel/mag into the inter-core ring. |
| **Core 1** | BMP390 polling at 10 Hz. SCD41 polling at 0.2 Hz (every 5 s). SGP41 raw measurement at ~1 Hz with Sensirion Gas Index Algorithm. 1PPS ISR + `$TM` parsing + timestamp computation. `$EN` formatting and TX at 10 Hz. `$SB` formatting and TX at 0.1 Hz. STM32 UART RX. |

### 2.2 Rationale

Madgwick at 100 Hz on a Cortex-M0+ takes ~80 µs per iteration (sqrt + 30 mults). Running it on Core 0 in isolation gives consistent timing free of jitter from the I2C SCD41/SGP41 transactions (which can stall ~50 ms during a forced read). Core 1 absorbs the slow sensor work and handles all output-side serialization so Core 0 stays deterministic.

The IMU data-ready interrupt (INT1 → GP6) provides the natural fusion tick. If INT1 is not wired (single-board prototypes), Core 0 falls back to a 100 Hz repeating-timer fired off RP2040's `add_repeating_timer_us(-10000, ...)`.

---

## 3. Timing — 1PPS Synchronization

Identical pattern to `branch_controller_wifi24_v1_0.md` §3, with Sensor-Branch specifics:

- PPS edge captured in EXTI on GP10 (same pin as WiFi24 BC).
- `$TM,epoch_s,fix_ok*XX\n` arrives on UART0 RX within ~100 ms of each edge.
- Sub-second time = epoch_s + (event_us − pps_timer_us) / 1 000 000.

### 3.1 `$EN` Timestamp Source

The `timestamp` field of every `$EN` is computed at the moment Core 1 *forms* the message — not at the IMU read time. The 100 ms output cadence is paced by a Core-1 repeating timer; the timestamp therefore advances in 100 ms steps locked to the PPS edge phase (rather than drifting with the IMU's free-running 100 Hz). This guarantees uniformly-spaced records in the analyzer's environment timeline.

### 3.2 PPS Loss

Same fallback as the WiFi24 BC §3.6: after 2 s of no edge, free-run from the last `epoch_s` using the internal crystal. `time_flag = 1` is stamped on every `$EN` until PPS resumes.

---

## 4. Sensor Drivers

### 4.1 ICM-42688-P (IMU)

```c
// Init (over I2C0, addr 0x68):
WHO_AM_I (0x75) == 0x47  // verify
DEVICE_CONFIG (0x11) = 0x01    // soft reset
delay 1 ms
PWR_MGMT0 (0x4E)  = 0x0F        // gyro + accel in LN mode
ACCEL_CONFIG0 (0x50) = 0x06     // ±8g, 100 Hz ODR
GYRO_CONFIG0 (0x4F)  = 0x06     // ±2000 dps, 100 Hz ODR
INT_CONFIG (0x14)    = 0x18     // INT1 push-pull, active-low, pulsed
INT_SOURCE0 (0x65)   = 0x08     // INT1 = UI data ready
delay 50 ms (settling)
```

Each fusion tick (100 Hz): burst-read 14 bytes from `TEMP_DATA1` (0x1D) — temperature, accel xyz, gyro xyz. Convert to SI (m/s², rad/s).

| Sensitivity | Value |
|---|---|
| Accel ±8g | 4096 LSB/g (16-bit) |
| Gyro ±2000 dps | 16.4 LSB/(°/s) |

### 4.2 LIS3MDL (Magnetometer)

```c
// Init (over I2C0, addr 0x1C):
WHO_AM_I (0x0F) == 0x3D  // verify
CTRL_REG1 (0x20) = 0x70   // temp off, ultra-high-perf XY, 10 Hz
CTRL_REG2 (0x21) = 0x00   // ±4 gauss
CTRL_REG3 (0x22) = 0x00   // continuous conversion
CTRL_REG4 (0x23) = 0x0C   // ultra-high-perf Z, LE byte order
```

Each magnetometer tick (10 Hz): read 6 bytes from `OUT_X_L` (0x28) auto-increment. Sensitivity ±4 gauss = 6842 LSB/gauss. Convert to µT (1 gauss = 100 µT).

The 10 Hz mag rate is upsampled to the 100 Hz fusion loop by holding the last mag sample between updates. Madgwick is robust to this asymmetric rate.

### 4.3 BMP390 (Barometer)

```c
// Init (over I2C0, addr 0x77):
CHIP_ID (0x00) == 0x60  // verify
CMD (0x7E) = 0xB6        // soft reset
delay 2 ms
PWR_CTRL (0x1B)   = 0x33  // press + temp enable, normal mode
OSR (0x1C)        = 0x03  // P×8, T×1 (recommended drone/UAV config)
ODR (0x1D)        = 0x02  // 50 Hz ODR
CONFIG (0x1F)     = 0x02  // IIR coeff 3
```

Polled at 10 Hz: read 6 bytes from `DATA_0` (0x04) — 24-bit press + 24-bit temp. Compensation per the datasheet's `bmp390_compensate_*` algorithms (calibration coefficients read once at init from regs 0x31..0x45).

Altitude (ISA model): `alt_m = 44330 * (1 - (P / P0)^(1/5.255))`, with `P0 = 101325 Pa`.

### 4.4 SCD41 (CO2 + Temp + Humidity)

```c
// Init (over I2C0, addr 0x62):
stop_periodic_measurement (cmd 0x3F86)  // in case prior session left it running
delay 500 ms
get_serial_number (cmd 0x3682)          // 9 bytes, verify communication
start_periodic_measurement (cmd 0x21B1) // 5-s cadence
```

Polled at 0.2 Hz (every 5 s + small margin): check `get_data_ready_status` (0xE4B8). When ready, read 9 bytes from `read_measurement` (0xEC05) — CRC-checked, 3 × (16-bit value + 8-bit CRC8).

Conversions per datasheet:
- CO2 [ppm] = raw_co2
- Temp [°C] = -45 + 175 × (raw_t / 65535)
- RH [%]   = 100 × (raw_rh / 65535)

The cached CO2/T/RH values are held in `$EN` for up to 6 s. Beyond 6 s of no fresh read, set `scd_ok = 0` and emit sentinels (`co2_ppm = -1`, `temp_c = NaN`, `humid_pct = NaN`).

### 4.5 SGP41 (VOC + NOx)

```c
// Init (over I2C0, addr 0x59):
get_serial_number (cmd 0x3682)            // verify
execute_self_test (cmd 0x280E)            // returns 3 bytes; check OK
sgp41_execute_conditioning (cmd 0x2612)   // 10 s warm-up; emit sentinels until done
delay until 10 s have elapsed since first measure
sgp41_measure_raw_signals (cmd 0x2619)    // RH and T compensation args
```

The `measure_raw_signals` command takes a 6-byte argument: 16-bit humidity (with CRC) + 16-bit temperature (with CRC), formatted from the latest SCD41 reading. If SCD41 has not produced data yet, pass datasheet defaults (50 %RH, 25 °C: `0x8000` and `0x6666`).

Returns 6 bytes: raw VOC (16-bit + CRC) + raw NOx (16-bit + CRC). Feed both into the Sensirion **Gas Index Algorithm** (BSD-licensed reference C from Sensirion) which produces the 1–500 index used in `$EN`.

The algorithm runs at ~1 Hz with raw signals collected at ~1 Hz. The output index is held between updates and emitted in every `$EN`.

### 4.6 Sensor Failure Handling

Each sensor is independently optional (per v2.2 §3.8 failure section):

- I2C transaction failure → set `<sensor>_ok = 0` in `$SB`, emit sentinels in `$EN`.
- Re-init attempt every 30 s for failed sensors.
- SCD41 failure: SGP41 falls back to default compensation values; flag both `scd_ok=0` and `voc_index` lower-confidence in analyzer (no firmware flag, just downstream interpretation).

---

## 5. Sensor Fusion — Madgwick AHRS

### 5.1 Implementation

Use the canonical Madgwick AHRS (Madgwick + Vaidyanathan 2010), fixed-point–free version using `float` (RP2040 has no hardware FPU; the M0+ runs single-precision at ~25 cycles/multiply — at 100 Hz × 30 ops/tick that's ~75 000 cycles/s, ~0.06% of one core).

Library: bring in the public-domain `MadgwickAHRS.c/.h` (released by SOH Madgwick) verbatim, plus a small wrapper:

```c
void fusion_tick(float ax, float ay, float az,
                 float gx, float gy, float gz,
                 float mx, float my, float mz) {
    MadgwickAHRSupdate(gx, gy, gz, ax, ay, az, mx, my, mz);
    // After call: q0, q1, q2, q3 are the updated quaternion globals.
}
```

### 5.2 Tuning

| Param | Value | Notes |
|---|---|---|
| β (Madgwick gain) | 0.1 | Standard for vehicle use; reduce to 0.04 if jitter is too high in straight-line cruise |
| sample frequency (`SAMPLE_FREQ`) | 100.0f | Must track the actual fusion rate; if INT1 is used, derive from inter-INT interval |
| Initial alignment | 1 s of accel-only + mag-only static average to seed quaternion | Reduces convergence time at startup |

### 5.3 Output

10 Hz decimated: heading (from quaternion Z-axis vs magnetic north), pitch, roll. Stored alongside the raw accel + mag samples in the inter-core ring entry.

```c
struct EnvSample {
    uint64_t local_timer_us;   // time_us_64 at sample formation
    float    heading_deg, pitch_deg, roll_deg;
    float    ax, ay, az;       // m/s² body frame
    float    mx, my, mz;       // µT raw
    // baro/scd41/sgp41 are filled by Core 1 at TX time, not Core 0
};
```

### 5.4 Magnetic Calibration

v1.0 uses **uncalibrated** magnetometer values into Madgwick. A `cal_status` flag (in `$SB` `err_count` upper bits, or a future spec amendment with a dedicated field) marks heading as "raw" until autocalibration is implemented.

Recommended Phase-3 path:
- Hard-iron offsets: accumulate min/max of mx/my/mz over a full vehicle rotation (~30 s of varied heading), compute `(min+max)/2` per axis as the hard-iron offset.
- Soft-iron scaling: full ellipsoid fit deferred; the diagonal-only approximation (`(max-min)/2` per axis) handles ~80% of soft-iron error.
- Store calibration in RP2040 flash (last 4 KB sector reserved for cal); reload on boot.

---

## 6. Inter-Core Communication

### 6.1 EnvSample Queue

Single SPSC ring buffer between Core 0 (producer) and Core 1 (consumer). Same pattern as `branch_controller_wifi24_v1_1_amendment.md` §4.5: producer owns `w_idx`, consumer owns `r_idx`, `__dmb()` barriers, no spinlocks.

| Symbol | Value |
|---|---|
| `ENV_RING_CAP` | 32 slots |
| Slot size | ~64 B |
| Total | 2 KB |

At a 100 Hz produce rate and 10 Hz consume rate, the natural occupancy is ~10 slots between dequeues. The 32-slot capacity gives ample headroom for I2C stalls on Core 1.

Overflow: producer drops the new sample, increments `err_count`. The 10 Hz output cadence is preserved by always emitting the latest cached values (the missed sample is just not used in the next `$EN`).

### 6.2 Slow-Sensor Cache (Core 1 only)

```c
struct SlowCache {
    // BMP390 (10 Hz fresh):
    float baro_hpa, alt_m;
    uint64_t baro_last_us;

    // SCD41 (0.2 Hz fresh; 6 s validity):
    int  co2_ppm;        // -1 if stale
    float scd_temp_c;
    float scd_humid_pct;
    uint64_t scd_last_us;

    // SGP41 (1 Hz fresh, requires SCD41 compensation):
    int voc_index, nox_index;  // -1 until conditioned
    bool sgp_conditioned;
    uint64_t sgp_last_us;
};
```

Read-only from `$EN` formatter, written by the Core-1 main loop after each sensor read.

---

## 7. Upstream Messages (RP2040 → STM32)

Same NMEA-style framing as all other Branches.

### 7.1 `$EN` — Environment Record

Replaced from v2.1 by `system_plan_v2_2_amendment.md` §3.8. 20 fields total. Emitted every 100 ms (10 Hz):

```
$EN,SEN,timestamp,heading,pitch,roll,accel_x,accel_y,accel_z,mag_x,mag_y,mag_z,baro_hpa,alt_m,temp_c,humid_pct,co2_ppm,voc_index,nox_index,time_flag*XX\n
```

Sentinel handling:
- Float fields: `NaN` (literal three-byte ASCII string `NaN`).
- Integer fields (`co2_ppm`, `voc_index`, `nox_index`): `-1`.

The 10 Hz emission cadence is paced by an `add_repeating_timer_us(-100000, ...)` callback that signals Core 1's main loop. The callback does no work other than setting a flag; the main loop formats and TX's on the next iteration.

### 7.2 `$SB` — Sensor Branch Status

Per `system_plan_v2_2_amendment.md` §3.8. 10 fields. Emitted every 10 s:

```
$SB,SEN,uptime_s,time_valid,imu_ok,mag_ok,baro_ok,scd_ok,sgp_ok,fusion_rate_hz,err_count*XX\n
```

`fusion_rate_hz` is the measured Madgwick tick rate over the prior 10 s window — should read 100 ± 1; deviation indicates INT1 jitter or I2C bus contention.

`err_count`: cumulative across all sources (I2C errors, Madgwick NaN guards triggering, queue overflows, checksum failures on RX).

---

## 8. Downstream Messages (STM32 → RP2040)

### 8.1 `$TM` — Time Message

Identical to `branch_controller_wifi24_v1_0.md` §3.2.

### 8.2 `$RC` — Relay Command

Not used by the Env Sensor Branch in v1.0 — the Sensor Branch has no Leaves. Future amendments may define `$RC,SEN,<cmd>` to push runtime parameters (e.g., re-trigger SGP41 conditioning, recalibrate magnetometer). For v1.0 the RP2040 silently drops any `$RC` it receives.

### 8.3 `$RQ` — Request Status

```
$RQ,SEN*XX\n
```

Triggers an immediate `$SB` emission outside the normal 10 s cadence. Useful for the Trunk to ping the Branch at startup.

---

## 9. Module Decomposition

```
env_sensor_branch/
├── CMakeLists.txt              pico-sdk + CMake
├── pico_sdk_import.cmake
├── README.md                   layout, build, flagged deviations
├── include/
│   └── env_defs.h              constants, structs, enums
├── third_party/
│   ├── MadgwickAHRS.{c,h}      verbatim from public-domain release
│   └── sensirion_gas_index_algorithm/   BSD-licensed VOC/NOx index lib
└── src/
    ├── main.c                  entry, core launch, init sequence, state machine
    ├── core0_fusion.{h,c}      IMU read + Madgwick 100 Hz + mag upsample + ring push
    ├── core1_output.{h,c}      slow sensors + $TM ingest + $EN/$SB TX + main loop
    ├── i2c_bus.{h,c}           I2C0 driver (shared, with recovery)
    ├── icm42688.{h,c}          IMU driver
    ├── lis3mdl.{h,c}           Magnetometer driver
    ├── bmp390.{h,c}            Barometer driver (incl. compensation)
    ├── scd41.{h,c}             SCD41 driver (CRC, periodic measurement)
    ├── sgp41.{h,c}             SGP41 driver + Gas Index wrapper
    ├── proto.{h,c}             framing, checksum, sentinel formatting (shared with BC family)
    ├── pps_time.{h,c}          1PPS ISR, $TM parse, timestamp computation
    ├── queues.{h,c}             inter-core SPSC ring (true SPSC, __dmb)
    └── stm32_uart.{h,c}        UART0 TX (polled) + RX line assembly
```

**Build system:** pico-sdk + CMake (identical to `branch_wifi24/`). Not Arduino — needs `multicore_launch_core1()`, custom I2C timing, and tight ISR control for the PPS edge.

---

## 10. State Machine — Sensor Branch

### 10.1 Top-Level States

```
[BOOT]
   │
   ├── Init clocks (125 MHz), UART0, UART1 (debug)
   ├── Init I2C0 @ 400 kHz
   ├── Init GP10 PPS EXTI
   ├── Init queues
   ├── Launch Core 1
   │
   ↓
[SENSOR_PROBE]
   │
   ├── For each sensor: WHO_AM_I / probe; mark <sensor>_ok
   ├── Configure responsive sensors
   ├── Kick off SGP41 conditioning (non-blocking, 10 s)
   │
   ↓
[WAIT_PPS] ────────────────────────────────────────┐
   │                                                │ (10 s timeout → proceed)
   │ (PPS edge received + $TM applied → time_valid) │
   ↓                                                ↓
[RUNNING] ←─────────────────────────────────────────┘
   │
   │ Core 0 loop:
   │   - on IMU INT1 (or 100 Hz timer): read IMU, mag (held), run Madgwick
   │   - every 10 ticks: push EnvSample to ring
   │
   │ Core 1 loop:
   │   - 10 Hz tick → drain ring, fetch slow cache, format $EN, TX
   │   - 0.1 Hz tick → format $SB, TX
   │   - 5 s tick → poll SCD41 if ready; update cache
   │   - 1 s tick → SGP41 measure_raw + gas index update; update cache
   │   - 100 ms tick → BMP390 poll; update cache
   │   - on $TM RX: apply epoch
   │   - on $RQ RX: emit $SB immediately
   │   - PPS health: if stale > 2 s, time_valid=0, time_flag=1 on all $EN
```

---

## 11. Memory Budget

| Allocation | Size | Notes |
|---|---|---|
| EnvSample ring | 2 KB | 32 × ~64 B |
| SlowCache | 128 B | |
| TimeState | 32 B | |
| Madgwick state | 32 B | quaternion + globals |
| I2C0 RX/TX scratch | 1 KB | |
| UART0 TX buffer | 512 B | |
| UART0 RX line buf | 200 B | |
| Sensirion Gas Index state (×2 channels) | ~1 KB | per Sensirion docs |
| BMP390 calibration coeffs | 22 B | |
| Stacks (Core 0 + Core 1) | 8 KB | |
| Debug UART buffer | 256 B | |
| **Total** | **~13 KB** | Out of 264 KB RP2040 SRAM |

Headroom: ~250 KB free.

---

## 12. Build Configuration

```cmake
cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(env_sensor_branch C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(env_sensor_branch
    src/main.c
    src/core0_fusion.c
    src/core1_output.c
    src/i2c_bus.c
    src/icm42688.c
    src/lis3mdl.c
    src/bmp390.c
    src/scd41.c
    src/sgp41.c
    src/proto.c
    src/pps_time.c
    src/queues.c
    src/stm32_uart.c
    third_party/MadgwickAHRS.c
    third_party/sensirion_gas_index_algorithm/sensirion_gas_index_algorithm.c
)

target_compile_definitions(env_sensor_branch PRIVATE
    SENSOR_FW_VERSION="1.0.0"
)

target_include_directories(env_sensor_branch PRIVATE
    include
    third_party
    third_party/sensirion_gas_index_algorithm
)

target_link_libraries(env_sensor_branch
    pico_stdlib
    pico_multicore
    hardware_i2c
    hardware_uart
    hardware_gpio
    hardware_irq
    hardware_timer
)

pico_enable_stdio_uart(env_sensor_branch 0)   # UART0 reserved for STM32
pico_enable_stdio_usb(env_sensor_branch 0)
# debug console: enable UART1 stdio if needed during bring-up
pico_add_extra_outputs(env_sensor_branch)
```

Build:

```bash
mkdir build && cd build
cmake -DPICO_BOARD=pico ..
make -j$(nproc)         # → build/env_sensor_branch.uf2
```

Flash by drag-and-drop or `picotool load`. Identical to the WiFi24 BC build/flash workflow.

---

## 13. Testing Procedure

### 13.1 Bench Test — Single Sensor at a Time

For each sensor in order (IMU → mag → baro → SCD41 → SGP41):
1. Disconnect all other sensors.
2. Flash firmware; observe debug UART boot log.
3. Verify the corresponding `<sensor>_ok` flag in `$SB` reads 1.
4. Verify the corresponding fields in `$EN` are non-sentinel and within plausible ranges:
   - Accel: |a| ≈ 9.8 m/s² at rest, gravity component on the down axis.
   - Mag: |B| ≈ 25–65 µT (varies with location).
   - Baro: ~1013 hPa at sea level; altitude ~0 m ± 30 m.
   - SCD41: 400–800 ppm indoor.
   - SGP41 VOC: 100 ± 50 after conditioning in clean air.

### 13.2 Fusion Sanity

1. Hold the board flat: pitch and roll should read ≈ 0°, heading should respond to physical rotation.
2. Rotate 360° on a level surface: heading should sweep through 360° smoothly.
3. Tilt to ±45°: pitch/roll should track within ±2° accuracy.

### 13.3 10 Hz Rate Check

1. Open USART6 of the STM32 (or wire directly to a logic analyzer).
2. Count `$EN` lines per second; verify 10 ± 0.1.
3. Verify timestamp deltas between consecutive `$EN` are ~100 000 µs ± jitter.

### 13.4 PPS Loss / Recovery

1. With PPS connected, verify `time_flag=0` on all `$EN`.
2. Disconnect PPS for 5 s, verify `time_flag` transitions to 1 within 2 s.
3. Reconnect PPS, verify `time_flag` returns to 0 within 1 s of the next PPS edge + `$TM`.

### 13.5 Full-Branch Integration

1. Wire to STM32 #1 USART6 per `stm32_h753_firmware_v1_0.md` §1.2.
2. Verify `n_branches_ok` includes SEN within 30 s of boot.
3. Verify the analyzer (when written) ingests `environment_records` rows with all 20 fields populated.

---

## 14. Open Items

| # | Item | Status |
|---|---|---|
| 1 | Magnetometer calibration (hard-iron, soft-iron, persisted to flash) | Deferred to Phase 3 |
| 2 | INT1 wiring vs timer-based 100 Hz fusion tick | PCB decision; both supported in firmware |
| 3 | Final GPIO pin assignments | Preliminary in §1 |
| 4 | SCD41 single-shot vs 5 s periodic mode trade-off | v1.0 uses periodic; revisit if cabin-CO2 response time is too slow |
| 5 | SGP41 algorithm state persistence across power cycles | Not persisted in v1.0 |
| 6 | Optional GPS on the Sensor Branch for INS dead-reckoning during tunnel/garage dropouts | Deferred to Phase 3+ |
| 7 | Vibration analysis (accelerometer FFT for road/mount quality) | Trunk-side analysis; sensor data is sufficient |
| 8 | Power budget (RP2040 + 5 sensors): not yet measured | Bench measurement pending |

---

## 15. Cross-References

- `system_plan_v2_1_amendment.md` §3.8 — Sensor Branch architecture and intent.
- `system_plan_v2_2_amendment.md` — sensor set (SCD41 + SGP41), `$EN` v2 (20 fields), `$SB` v2, `environment_records` schema.
- `stm32_h753_firmware_v1_0.md` — upstream side; this Branch sits on STM32 #1 USART6.
- `branch_controller_wifi24_v1_0.md` + v1.1 amendment — borrowed patterns for SPSC, timestamping, PPS handling, proto framing.
