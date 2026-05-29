# `env_sensor_branch` — Environmental Sensor Branch Firmware

RP2040 firmware for the Env Sensor Branch (single-board, no Leaves). Implements `env_sensor_branch_v1_0.md`. Streams `$EN` at 10 Hz and `$SB` at 0.1 Hz on UART0 to STM32 #1 USART6.

## Target

- **MCU:** RP2040
- **SDK:** pico-sdk + CMake (not Arduino — needs `multicore_launch_core1()`, custom I2C timing, EXTI).
- **Build target:** `env_sensor_branch.uf2`.

## Sensors

| Sensor | Module | I2C addr | Rate | Source files |
|---|---|---|---|---|
| IMU | ICM-42688-P | 0x68 | 100 Hz | `src/icm42688.{h,c}` |
| Magnetometer | LIS3MDL | 0x1C | 10 Hz | `src/lis3mdl.{h,c}` |
| Barometer | BMP390 | 0x77 | 10 Hz | `src/bmp390.{h,c}` |
| CO2 + Temp + RH | SCD41 | 0x62 | 0.2 Hz (5 s) | `src/scd41.{h,c}` |
| VOC + NOx | SGP41 | 0x59 | ~1 Hz | `src/sgp41.{h,c}` |

## Sensor fusion

- **Madgwick AHRS** at 100 Hz, decimated to 10 Hz, in `src/core0_fusion.c`.
- The vendored public-domain Madgwick implementation lives in `third_party/MadgwickAHRS.{c,h}`.
- The Sensirion Gas Index Algorithm is **stubbed** in `src/sgp41.c` — it returns the algorithm baseline (`voc_index = 100`, `nox_index = 1`) when SGP41 is conditioned, and `-1` otherwise. To upgrade to the real algorithm:
  1. `git clone https://github.com/Sensirion/gas-index-algorithm third_party/sensirion_gas_index_algorithm`
  2. Add `third_party/sensirion_gas_index_algorithm/sensirion_gas_index_algorithm.c` to `CMakeLists.txt`.
  3. Replace `sgp41_run_gas_index()` body in `src/sgp41.c` with the Sensirion `GasIndexAlgorithm_process()` calls.

## Build

Same toolchain as `branch_wifi24`. From this directory:

```
mkdir build && cd build
cmake -DPICO_BOARD=pico ..
make -j$(nproc)         # → build/env_sensor_branch.uf2
```

## Pin map

| Pin | Function |
|---|---|
| GP4 / GP5 | I2C0 SDA / SCL (all 5 sensors share this bus) |
| GP6 | ICM-42688-P INT1 (data ready, optional fast-path) |
| GP10 | 1PPS input (EXTI) |
| GP12 / GP13 | STM32 UART0 TX / RX (USART6 on STM32 #1) |
| GP16 | UART1 TX (optional debug console) |

## Bench test

See `MKII_handoff/specs/env_sensor_branch_v1_0.md` §13 for the full procedure. Quick smoke:

1. Wire to STM32 #1 USART6 and connect 1PPS.
2. Flash; verify `$SB` arrives every 10 s with `imu_ok`, `mag_ok`, `baro_ok`, `scd_ok`, `sgp_ok` reflecting which sensors are present.
3. Verify `$EN` arrives at 10 Hz with `time_flag=0` once PPS + `$TM` are synced.
4. Tilt the board and verify `pitch_deg`/`roll_deg` track plausibly.
