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
cmake -G Ninja -DPICO_BOARD=pico ..                       # default: SD stub
# ...or the real FatFs-over-SPI backend (durable logging):
cmake -G Ninja -DPICO_BOARD=pico -DAGG_SD_BACKEND=fatfs ..
ninja agg_lite          # -> agg_lite.uf2
```

CI: `.github/workflows/agg_lite.yml` (pico-sdk 2.1.0) builds **both** backends
(`stub` and `fatfs`) in a matrix, matching the other RP2040 trees.

## SD logging — two selectable backends

`sd_spi_fatfs.{c,h}` is the seam between the portable session/ring logic and the
physical card driver. There are **two implementations of the one interface**,
selected with `-DAGG_SD_BACKEND=`:

| `AGG_SD_BACKEND` | What it links | Durable? |
|---|---|---|
| `fatfs` | the vendored FatFs-over-SPI driver (real writes) | yes (HW-verification-pending) |
| `stub` (default) | a placeholder that accounts bytes but discards them | no |

**`fatfs` — the real backend.** Built on the vendored carlk3
`no-OS-FatFS-SD-SPI-RPi-Pico` (`third_party/pico_fatfs_spi/`, **Apache-2.0**,
pinned commit `196016f525e5b9c161f2b965ddd3045a4ef87649`). The SPI0 bus + card
wiring (§1: SCK GP18 / MOSI GP19 / MISO GP16 / CS GP17 / CD GP22) is supplied by
the app in `src/sd_hw_config.c`; `src/sd_spi_fatfs.c` maps the interface onto
FatFs (`f_mount` / `f_open(FA_OPEN_APPEND)` / `f_write` / `f_sync` / `f_mkdir` /
`f_rename`). The persistence path (§8) — session dir from first fix, pending
bucket renamed at first fix, open-once/write-many, batched drain of the 32 KB RAM
ring, `f_sync` per flush, `FR_DISK_ERR` re-mount on hot remove/insert — drives
this backend, and `$LA.sd_ok` / `$LA.sd_kb` reflect the real card.

> **Status: compiled + linked, HARDWARE-VERIFICATION-PENDING.** CI builds and
> links `agg_lite.uf2` with `-DAGG_SD_BACKEND=fatfs`, which proves it *builds and
> links* — **not** that a card is actually written. That requires the bench step
> in `lite_aggregator_v1_0.md` §13.3 (walk a dense environment, pull the card,
> confirm `records.log` is well-formed and `f_sync`'d). The CD polarity and SPI
> baud in `sd_hw_config.c` are preliminary pending that bring-up.

**`stub` — the default placeholder.** Brings up SPI0 + card-detect and accounts
writes (byte counter) but does **not** persist them, so `agg_lite.uf2` builds in
CI and the entire ring / session / state-machine / `$LA` path is exercised on the
CDC mirror without a card present. It is **not** a durable log. (Same convention
as the STM32 PAL host-smoke stub, `gps_push_config()` no-op, SGP41 gas-index stub.)

The vendored FatFs `ffconf.h` is used **as shipped** (it already enables
`f_mkdir`/`f_rename` and long filenames); nothing under `FatFs_SPI/` was modified.

## Deviations / open items (from the spec)

- **GPS transport is UART1**, not the STM32's I2C path (§1, open item 1 — decision pending).
- **No 2.4 GHz WIDS** in single-Leaf config; no `$ET`/`$DF` (§6.1, open item 2).
- **BT Classic deferred**; no `$BC_T`, BLE only (§6.3, `AGG_BLE_BT_CLASSIC=0`).
- **`gps_push_config()` is a no-op**; module pre-configured via u-center (§5, open item 6).
- **SD has two backends** — the real vendored FatFs-over-SPI (`fatfs`, durable, HW-verification-pending) and a non-durable `stub` (default). See above (§14 item 5 retired).
- GPIO assignments are preliminary, pending PCB layout (§1, open item 9).
- **USB CDC must be initialized on core 0** (`stdio_usb_init()` in `main()` before the core1 launch), not in `usb_cdc_mirror_init()` on core 1. The SDK drives `tud_task()` from a repeating timer on the default alarm pool (created on core 0) while the servicing IRQ is enabled on whichever core calls `stdio_usb_init()`; splitting them across cores leaves the device un-enumerated (board runs, LED blinks, no `/dev/ttyACM`). Verified on a Pico (RP2040 B2): `2e8a:000a` enumerates and `$LA` streams.
