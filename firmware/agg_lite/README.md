# agg_lite — Light-Duty Single-Box Aggregator (RP2040)

Single-MCU variant of the MKII aggregator for **fixed-position** use. One RP2040
ingests three Leaves directly — 2.4 GHz (scan-hop), 5 GHz (scan), BLE — disciplines
time from its own GPS + 1PPS, deduplicates per family, and logs the **identical
upstream record schemas** the multi-box system produces to microSD (plus an
optional live USB-CDC mirror). It collapses the Branch-Controller and STM32
mid-tier into one MCU; there is no Jetson Trunk.

Implements **`lite_aggregator_v1_0.md`**. Depends on the **`wifi24_leaf_protocol_v1_3`
amendment** (Scan-Hop `mode=2`) on the 2.4 GHz Leaf, the existing 5 GHz Leaf in
scan mode, and the new **`leaf_ble`** firmware.

## Layout

| Module | Origin | Role |
|---|---|---|
| `proto.{c,h}`, `pio_uart.{c,h}`, `pio/*.pio` | reuse: `branch_wifi24` | framing, XOR checksum, PIO UART (PIO0 RX ×3, PIO1 TX retarget, F-001) |
| `queues.{c,h}` | adapted | SPSC rings: 2× WiFi detection + BLE event + BLE-ext |
| `pps_time.{c,h}` | reuse + local `$TM` | 1PPS ISR (F-004 atomic snapshot) + GPS-driven `$TM` generation |
| `gps.{c,h}` | port: `stm32_aggregator/App/gps.c` | NMEA RMC/GGA, checksum + range validate (F-003), UART1 |
| `leaf_health.{c,h}`, `leaf_cmd.{c,h}` | reuse | 3-Leaf watchdog/recovery; per-family `$CF` |
| `dedup_wifi.{c,h}` | reuse (re-entrant) | WiFi AP dedup, instanced ×2 (W24 + W5G), 500 ms tombstone |
| `dedup_ble.{c,h}` | reuse: blebt §9.2 | BLE dedup, 5 s window, 1024 entries, tombstone |
| `upstream_fmt.{c,h}` | reuse | `$WA`/`$BD`/`$BX` + `$LA` consolidated heartbeat (23 fields) |
| `sd_log.{c,h}` | port: `stm32_aggregator/App/sd_log.c` | session mgmt + RAM ring + batched drain |
| `sd_spi_fatfs.{c,h}` | **NEW** | SD backend interface + placeholder (see below) |
| `usb_cdc_mirror.{c,h}` | adapted | optional live CDC mirror + mode detect |
| `core0_leaf_io.{c,h}` | adapted | 3 PIO links, RX assembly, per-family dispatch |
| `core1_main.{c,h}` | adapted | GPS/`$TM`, 3× dedup flush, format, SD enqueue, `$LA`, state machine |

## Build

```sh
mkdir -p build && cd build
cmake -G Ninja -DPICO_BOARD=pico ..
ninja agg_lite          # -> agg_lite.uf2
```

CI: `.github/workflows/agg_lite.yml` (pico-sdk 2.1.0), matching the other RP2040 trees.

## SD logging — backend status (important)

`sd_spi_fatfs.{c,h}` is the seam between the portable session/ring logic and the
physical **FatFs-over-SPI** card driver. `lite_aggregator_v1_0.md` §9 lists this
glue as the one genuinely new subsystem (`[NEW vendored]`, e.g. carlk3's
`no-OS-FatFS-SD-SPI-RPi-Pico`) and §14 item 5 flags library selection +
write-stall headroom as **implementation-phase** work.

**The default build links a documented PLACEHOLDER backend** (`AGG_SD_BACKEND=stub`)
so `agg_lite.uf2` compiles in CI and the entire ring / session / state-machine /
`$LA` path is exercised end-to-end on the CDC mirror. The placeholder brings up
SPI0 + card-detect and **accounts** writes (byte counter) but does **not** persist
them — it is **not** a durable log. This mirrors the repo's existing pattern (the
STM32 PAL host-smoke stub, `gps_push_config()` no-op, the SGP41 gas-index stub).

To get durable logging:

1. Vendor the FatFs-over-SPI library into `third_party/pico_fatfs_spi/`.
2. Implement the `fatfs` branch of `sd_spi_fatfs.c` against its `f_mount`/`f_open`/
   `f_write`/`f_sync` API (a thin adapter — nothing above `sd_spi_fatfs.h` changes).
3. Configure with `-DAGG_SD_BACKEND=fatfs` and link the library in `CMakeLists.txt`.

## Deviations / open items (from the spec)

- **GPS transport is UART1**, not the STM32's I2C path (§1, open item 1 — decision pending).
- **No 2.4 GHz WIDS** in single-Leaf config; no `$ET`/`$DF` (§6.1, open item 2).
- **BT Classic deferred**; no `$BC_T`, BLE only (§6.3, `AGG_BLE_BT_CLASSIC=0`).
- **`gps_push_config()` is a no-op**; module pre-configured via u-center (§5, open item 6).
- **SD backend is a placeholder** pending the vendored FatFs library (§14 item 5; above).
- GPIO assignments are preliminary, pending PCB layout (§1, open item 9).
