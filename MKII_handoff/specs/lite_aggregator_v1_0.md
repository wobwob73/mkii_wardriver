# Light-Duty Single-Box Aggregator — RP2040 Firmware Guide

**Version:** 1.0.0
**Date:** 2026-06-02
**Scope:** RP2040 firmware for the light-duty MKII variant — a single aggregator that ingests three Leaves directly (2.4 GHz WiFi, 5 GHz WiFi, BLE), disciplines time from its own GPS, and logs to microSD. Collapses the Branch-Controller and STM32 mid-tier into one MCU; no Jetson Trunk.
**Dependencies:** Leaf protocols — `wifi24_leaf_protocol_v1_1.md` + v1.2 amendment (W24 Leaf), `wifi5_branch_v1_0.md` §3–§8 (W5G Leaf), `blebt_branch_v1_0.md` §3–§7 (BLE Leaf). BC architecture reused — `branch_controller_wifi24_v1_0.md` + v1.1/v1.2 amendments. SD-logging and GPS logic ported from `stm32_h753_firmware_v1_0.md` §4 and §8. This document states only the deltas from those baselines; shared infrastructure (NMEA framing, `$CF`/`$CH`/`$PG`/`$RB`, `$HB`, SPSC queues, tombstone dedup, PIO0=RX/PIO1=TX, 1PPS + `$TM`, per-family upstream record schemas) is referenced by section, not restated.

---

## 1. Hardware Interface Summary

```
                          RP2040 (single box)
                    ┌──────────────────────────┐
  Leaf W24 TX ───→  │ GP1  (PIO0 SM0 RX)        │
  Leaf W24 RX ←───  │ GP0  (PIO0 SM0 TX) ──┐    │
  Leaf W5G TX ───→  │ GP3  (PIO0 SM1 RX)   │    │  TX state machines on
  Leaf W5G RX ←───  │ GP2  (PIO0 SM1 TX) ──┤    │  PIO1 SM0–SM2 (see §12.1)
  Leaf BLE TX ───→  │ GP5  (PIO0 SM2 RX)   │    │
  Leaf BLE RX ←───  │ GP4  (PIO0 SM2 TX) ──┘    │
                    │                          │
  GPS TX ────────→  │ GP9  (UART1 RX)           │
  GPS RX ←────────  │ GP8  (UART1 TX)           │
  GPS 1PPS ──────→  │ GP10 (GPIO IRQ, rising)   │
                    │                          │
  SD SCK ←────────  │ GP18 (SPI0 SCK)           │
  SD MOSI ←───────  │ GP19 (SPI0 TX)            │
  SD MISO ───────→  │ GP16 (SPI0 RX)            │
  SD CS ←─────────  │ GP17 (SPI0 CSn, GPIO)     │
  SD CD ─────────→  │ GP22 (GPIO, card-detect)  │  optional, pull-up
                    │                          │
  (live mirror)     │ USB   (native, CDC)       │  optional, see §2
  (debug) ←───────  │ GP12 (UART0 TX)           │  optional, 115200 8N1
                    └──────────────────────────┘
```

| Interface | Peripheral | Baud/Config | Purpose |
|---|---|---|---|
| 3× Leaf UARTs | PIO0 SM0–SM2 (RX) + PIO1 SM0–SM2 (TX) | 230400 8N1 | Leaf communication (bidirectional) |
| GPS | UART1 | 38400 8N1 NMEA | UTC second + position; per-NMEA checksum validated |
| 1PPS input | GPIO10 interrupt | Rising edge | Sub-second timestamp discipline |
| microSD | SPI0 + GPIO CS | up to ~25 MHz | Durable record log (durable sink) |
| Live mirror | USB CDC (native) | — | Optional real-time stream to a host |
| Debug output | UART0 (optional) | 115200 8N1 | Development logging, disabled in production |

Pin assignments are preliminary — subject to PCB layout. The Leaf links occupy GP0–GP5 (matching the lowest three slots of the BC pinout); UART0 (GP12/13) is free because there is no STM32 upstream, and is repurposed as the optional debug UART.

GPS transport here is **UART** (UART1), not the I2C path the STM32 uses (`stm32_h753_firmware_v1_0.md` §4.1). This matches the bring-up-simplicity precedent and is flagged for reconsideration in §14.

---

## 2. Operating Concept — Tier Collapse

This variant removes two tiers from the full architecture. In the full system: Leaf → Branch Controller (dedup, `$TM`, WIDS, upstream format) → STM32 (GPS, 1PPS source, SD in standalone mode) → Jetson. Here a single RP2040 plays the **Branch-Controller role for three protocol families simultaneously** and the **STM32 role** (own GPS, own 1PPS, SD logging) for itself.

**Why a single funnel is acceptable here.** The standalone payload concept that motivates this build avoids one node funneling others to dodge a throughput bottleneck — but that reasoning applies to funneling *raw frames*. MKII Leaves emit digested, deduplicated NMEA records. `stm32_h753_firmware_v1_0.md` §11 budgets the entire multi-branch upstream at ~5 KB/s; three families on one RP2040 are well inside that. The funnel is not the limiter.

**Modes** (parallel to `stm32_h753_firmware_v1_0.md` §2):

| Mode | USB CDC host? | SD present? | Behavior |
|---|---|---|---|
| **Standalone** | No | Required | All records logged to SD; LED slow blink. No live stream. |
| **Connected** | Yes | Optional (backup) | Records mirrored to USB CDC live; SD logs in parallel if mounted. |

SD is the durable record; the CDC mirror is the live one. Loss of the host never loses data.

---

## 3. Dual-Core Architecture

### 3.1 Core Assignment

| Core | Responsibilities |
|---|---|
| Core 0 | 3× PIO UART Leaf links (RX assembly, dispatch, TX), Leaf health watchdog + recovery, downstream command construction (`$CF`/`$CH`/`$PG`/`$RB`). Per `branch_controller_wifi24_v1_0.md` §5, scaled from 4 links to 3. |
| Core 1 | 1PPS ISR + GPS NMEA reader + local `$TM` discipline; three per-family dedup pipelines (W24, W5G, BLE) + WIDS; per-family upstream record formatting; SD writer draining a RAM ring. |

### 3.2 Rationale

The split is the BC's Core 0 / Core 1 split (`branch_controller_wifi24_v1_0.md` §2) with two changes: Core 1 absorbs the GPS reader and `$TM` *generation* that previously lived on the STM32 (the BC only *consumed* `$TM`), and Core 1's "upstream TX" is replaced by the SD writer. Both added Core 1 duties are I/O-bound and tolerate the dedup-flush cadence; the latency-critical work (per-byte RX, line assembly) stays on Core 0. The SD writer must never block Core 0 — see §8.2.

---

## 4. Timing — Own GPS, 1PPS + Local `$TM`

In the full system the BC received `$TM` over its upstream UART. Here the aggregator **is** the time authority: it captures its own GPS 1PPS edge and reads the GPS NMEA UTC second, then generates `$TM` internally and feeds it to the same timestamp machinery.

- **1PPS capture** — `branch_controller_wifi24_v1_0.md` §3.1/§6.1, GP10 rising-edge IRQ, atomic snapshot accessor (review finding F-004). On each edge, latch the timer count; the next whole UTC second from GPS RMC/GGA associates the edge with `epoch_s`.
- **Local `$TM` emission** — replaces `stm32_h753_firmware_v1_0.md` §3.3. After each PPS edge, once a valid UTC second is in hand, an internal `$TM` is posted to the timestamp state used by all three dedup pipelines. No `$TM` is transmitted on any wire (there is no downstream tier); it is an internal event.
- **Timestamp computation** — `branch_controller_wifi24_v1_0.md` §3.4. `time_flag` semantics unchanged: `0` = PPS-synced, `1` = degraded (free-running between/without edges).
- **PPS loss** — `branch_controller_wifi24_v1_0.md` §3.6 and `stm32_h753_firmware_v1_0.md` §3.4 both apply; on PPS-stale, fall back to free-running and mark `time_flag=1`.

---

## 5. GPS Interface

Logic ported from `stm32_h753_firmware_v1_0.md` §4; transport changed from I2C to UART1.

- **Parsing** — `$GxRMC` (UTC second-of-day, fix-valid, lat/lon) and `$GxGGA` (fix quality, sat count, altitude). NMEA checksum **and** field range validated before any timebase update (review finding F-003). Position state snapshotted into each record's path the way §4.3 of the STM32 spec describes; the W24/W5G/BLE upstream records do not themselves carry position (the analyzer joins on `timestamp`), so position rides on the heartbeat (`$LA`, §7.5).
- **Module** — BN-220-class or M10-class NMEA + 1PPS module. NMEA RMC + GGA at 1 Hz, PPS 1 Hz / rising edge.
- **Pre-config** — `gps_push_config()` is a **no-op** in v1.0.x (same placeholder and rationale as `stm32_h753_firmware_v1_0.md` §14 item 2): the module is pre-configured once via u-center. Target v1.1 to drive UBX `CFG-VALSET` at boot. The fix gate (§10) tolerates a factory-default module.

---

## 6. Leaf Ingest — Three Personalities

The aggregator runs three independent ingest pipelines keyed by Leaf slot. Each reuses its family's existing dedup + (optional) WIDS + upstream-format code unchanged; only the slot-to-family binding and the reduced Leaf count are new.

| Slot | PIO SM | Leaf MCU | `leaf_id` | Mode | Records consumed | Records produced (logged) |
|---|---|---|---|---|---|---|
| W24 | SM0 | ESP32-C3 | `W24` | **scan-hop** (rotate ch 1/6/11) | `$AP`, `$BK`, `$HB` | `$WA`, `$WP` |
| W5G | SM1 | ESP32-C5 | `W5_1` | scan (UNII-1+2A set, dwell ~200 ms) | `$AP`, `$BK`, `$HB` | `$WA`, `$WP` (branch_id `W5G`) |
| BLE | SM2 | ESP32-S3 | `BLE-1` | BLE passive (1M + Coded) | `$BL`, `$BX`, `$BK`, `$HB` | `$BD`, `$BX` |

### 6.1 W24 Leaf — single-Leaf coverage

The full Branch covers 2.4 GHz with three parked scan Leaves (W1/W2/W3 on 1/6/11) plus a fourth WIDS Leaf. A single Leaf cannot park three channels. The W24 Leaf therefore runs a **scan-hop** mode: round-robin ch 1 → 6 → 11 with per-channel dwell, emitting `$AP` per sweep. This is a Leaf-firmware change (today's scan mode parks on the single channel from `$CF`; WIDS mode hops promiscuously). It is specified as an amendment to the WiFi24 Leaf protocol/firmware, not here — see §15. The aggregator side is unchanged: it builds `$WA` from `$AP` exactly as `branch_controller_wifi24_v1_0.md` §6.3 does.

**WIDS is not available in the single-Leaf W24 configuration.** Evil-twin (`$ET`) and deauth-flood (`$DF`) detection require the promiscuous WIDS Leaf, which this variant omits. The aggregator's WIDS pipeline is compiled but receives no WIDS-class input on W24 and emits no `$ET`/`$DF`. Adding 2.4 GHz WIDS requires either a second W24 Leaf or a promiscuous-derived `$WA` path; see §14.

### 6.2 W5G Leaf — single-Leaf coverage

One W5_1 Leaf scanning the UNII-1 + UNII-2A set (`wifi5_branch_v1_0.md` §2.1). UNII-2C/UNII-3 (W5_2's set) and 5 GHz WIDS (W5_3) are omitted in light-duty; the aggregator tolerates their absence exactly as `wifi5_branch_v1_0.md` §3 states the BC tolerates missing Leaves. To cover the full 5 GHz set with one Leaf, switch it to channel-set ID 7 (all 5 GHz) at the cost of a ~2.5 s sweep — a `$CF` parameter choice, not a firmware change.

### 6.3 BLE Leaf — single-Leaf, BLE-only

One BLE-1 Leaf (`blebt_branch_v1_0.md` §7), NimBLE passive, 1M + Coded PHYs, three primary adv channels. **BT Classic is deferred** in light-duty (the `leaf_bt_classic` / `$BT` / `$BC_T` path is omitted). The aggregator runs the BLE dedup pipeline (`blebt_branch_v1_0.md` §9.2, 5 s window, 1024 entries, tombstone) and emits `$BD` + `$BX` pass-through. No `$BC_T`.

### 6.4 Identity & boot config

`$CF` assigns identity per the existing per-family boot sequences (`branch_controller_wifi24_v1_0.md` §5.7, `wifi5_branch_v1_0.md` §6, `blebt_branch_v1_0.md` §6). Concretely the aggregator sends, after time is valid (§10):
- `$CF,W24,2,<dwell>,...` — scan-hop mode (new mode id per the WiFi24 Leaf amendment, §15).
- `$CF,W5_1,0,3,200,0` — 5 GHz scan, UNII-1+2A.
- `$CF,BLE-1,0,3,1000,1000` — BLE both PHYs, 100% duty.

---

## 7. Record Schemas

The aggregator produces the **identical upstream record schemas** the multi-box system produces, so an existing capture is byte-compatible with the Analyzer regardless of which topology generated it. No per-record fields change; only the producer changes. References:

| Record | Schema source | branch_id |
|---|---|---|
| `$WA`, `$WP` | `branch_controller_wifi24_v1_0.md` §7.1–§7.2 | `W24` |
| `$WA`, `$WP` | `wifi5_branch_v1_0.md` §10 | `W5G` |
| `$BD`, `$BX` | `blebt_branch_v1_0.md` §10.1–§10.2 | `BLE` |

Not produced in light-duty v1.0: `$ET`, `$DF` (no WIDS Leaves — §6.1/§6.2), `$BC_T` (no BT Classic — §6.3).

### 7.5 `$LA` — Lite Aggregator Heartbeat

Replaces the three separate per-Branch `$BS` records and the STM32 `$AG`. One consolidated heartbeat every 10 s, written to SD (and CDC mirror if Connected). Carries the box-level position so the Analyzer can place the session even though detection records join on timestamp only.

```
$LA,timestamp,uptime_s,mode,time_valid,fix_ok,sat_count,lat,lon,alt,pps_age_ms,w24_st,w5g_st,ble_st,sd_ok,sd_kb,q_w24,q_w5g,q_ble,dedup_w24,dedup_w5g,dedup_ble,err_count*XX\n
```

| Field | Type | Description |
|---|---|---|
| timestamp | float | UTC epoch, 6 decimals; `0` until first fix |
| uptime_s | int | Seconds since boot |
| mode | int | 0 = Standalone, 1 = Connected |
| time_valid | int | 1 if PPS-synced time valid |
| fix_ok | int | 1 if GPS has a 3D fix |
| sat_count | int | Satellites used |
| lat | float | Decimal degrees; `0` if no fix |
| lon | float | Decimal degrees; `0` if no fix |
| alt | float | Metres MSL; `0` if no fix |
| pps_age_ms | int | Milliseconds since last PPS edge |
| w24_st | int | W24 Leaf: 0=offline, 1=online, 2=degraded |
| w5g_st | int | W5G Leaf status |
| ble_st | int | BLE Leaf status |
| sd_ok | int | 1 if SD mounted and writes succeeding |
| sd_kb | int | KB written this session |
| q_w24 | int | W24 detection-queue occupancy |
| q_w5g | int | W5G detection-queue occupancy |
| q_ble | int | BLE detection-queue occupancy |
| dedup_w24 | int | Current entry count, W24 dedup table |
| dedup_w5g | int | Current entry count, W5G dedup table |
| dedup_ble | int | Current entry count, BLE dedup table |
| err_count | int | Cumulative: queue overflows + checksum errors + SD write errors |

**Field count:** 23.

---

## 8. SD Card Logging

Logic ported from `stm32_h753_firmware_v1_0.md` §8; the SDMMC HAL backend is replaced by an SPI FatFs backend (the one genuinely new subsystem in this tree — §9).

### 8.1 File Layout

```
/<session_id>/
    records.log        # all upstream records ($WA/$WP/$BD/$BX) + $LA, one per line
```

`session_id` = `YYYYMMDD_HHMMSS` of the **first valid GPS fix** (`stm32_h753_firmware_v1_0.md` §8.1). Until the first fix, records go to a `pending` bucket and the directory is renamed when the first fix lands. This is the only behavioral gate inherited from the standalone-payload "no log before initial fix" rule (`Project Summary` firmware note) — restated in §10.

### 8.2 Write Strategy

- **RAM ring + writer task.** Core 1's dedup/format stages enqueue formatted lines into a RAM ring; the SD writer drains it in batched, block-aligned writes. A write stall (a slow card can block 100 ms+) must not stall ingest — the ring absorbs the burst, with a drop counter and high-water mark surfaced in `$LA.err_count` (the payload-doc buffering model).
- **Durability.** `f_sync` after each flush; worst-case loss on hard power-down is the unsynced tail (≤1 s of records).
- **Handle reuse.** Open once, write many; re-open on `FR_DISK_ERR` (hot remove/insert).

### 8.3 SD Absent / Failure

If the card is absent at boot or `f_mount` fails: in Standalone mode this is fatal-to-purpose — fast-blink the LED and keep retrying mount every 5 s; ingest continues into the ring (and is lost if never mounted). In Connected mode, continue streaming to USB CDC and retry mount in the background.

---

## 9. Module Decomposition

```
agg_lite/
├── CMakeLists.txt              # RP2040 SDK build → agg_lite.uf2
├── src/
│   ├── main.c                  # entry, core launch, init sequence, state machine
│   ├── core0_leaf_io.{h,c}     # [reuse: branch_wifi24] 3 PIO UART links, assembly, dispatch
│   ├── core1_main.{h,c}        # [adapted] main loop: $TM, 3× dedup flush, format, SD enqueue
│   ├── pio_uart.{h,c}          # [reuse] PIO UART driver (init, DMA, read/write)
│   ├── proto.{h,c}             # [reuse] framing, XOR checksum, field parse, hex codec
│   ├── leaf_health.{h,c}       # [reuse] Leaf state, watchdog, recovery (3 Leaves)
│   ├── leaf_cmd.{h,c}          # [reuse] $CF/$CH/$PG/$RB construction
│   ├── pps_time.{h,c}          # [reuse] 1PPS ISR + timestamp; + local $TM generator
│   ├── gps.{h,c}               # [port: stm32 App/gps.c] NMEA RMC/GGA, checksum, fix gate (UART1)
│   ├── dedup_wifi.{h,c}        # [reuse] WiFi AP dedup (instanced ×2: W24, W5G)
│   ├── dedup_ble.{h,c}         # [reuse: blebt] BLE dedup (5 s, 1024 entries, tombstone)
│   ├── upstream_fmt.{h,c}      # [reuse] $WA/$WP (W24,W5G) + $BD/$BX (BLE) + $LA
│   ├── sd_log.{h,c}            # [port: stm32 App/sd_log.c] session mgmt, ring, batched writes
│   ├── sd_spi_fatfs.{h,c}      # [NEW] FatFs glue over SPI0 (Pico SPI-SD library)
│   ├── usb_cdc_mirror.{h,c}    # [adapted] optional live mirror, mode detect
│   └── queues.{h,c}            # [reuse] SPSC ring buffers
├── pio/
│   ├── uart_tx.pio             # [reuse]
│   └── uart_rx.pio             # [reuse]
├── third_party/
│   └── pico_fatfs_spi/         # [NEW vendored] e.g. carlk3 no-OS-FatFS-SD-SPI-RPi-Pico
└── include/
    └── agg_defs.h              # constants, structs, enums, AGG_LITE_FW_VERSION
```

`[reuse]` modules are lifted from `branch_wifi24` (and `blebt` dedup) substantively unchanged. `[port]` modules carry over the C logic from the STM32 `App/` layer with a Pico backend. `[NEW]` is the SPI-FatFs glue. Build system: RP2040 C SDK + CMake — dual-core launch, PIO, DMA, and hardware spinlocks all require it.

---

## 10. State Machine

```
[BOOT]
    ├── Init PIO UARTs (3), UART1 (GPS), GP10 (PPS IRQ), SPI0 (SD)
    ├── Init queues, 3 dedup tables, RAM ring
    ├── f_mount SD  → LED3 + retry loop if absent (§8.3)
    ├── Launch Core 1
    ↓
[WAIT_TIME] ──────────────────────────────────────────┐
    │ (PPS edge + GPS UTC second → time valid)         │ (20 s timeout → proceed, time_flag=1)
    ↓                                                  ↓
[WAIT_FIX] (gate: no records logged before first 3D fix, HDOP<5) … pending bucket until fix
    ↓ (first valid fix → session_id set, dir renamed)
[INIT_LEAVES]
    ├── $CF to W24, W5_1, BLE-1 (§6.4)
    ├── await $HB (5 s/Leaf, 3 retries); tolerate any Leaf absent
    ↓
[RUNNING]
    ├── Core 0: 3× Leaf I/O loop
    ├── Core 1: $TM + dedup flush + format + SD write + $LA every 10 s
    ↓ (never exits)
```

`WAIT_TIME` parallels the BC's `WAIT_PPS` (`branch_controller_wifi24_v1_0.md` §10.2). The added `WAIT_FIX` log gate is the standalone-payload rule: nothing is written to a session file until the GPS *initially* establishes a fix; the gate is initial-establishment only, so later GPS duty-cycling or fix loss does not stop logging (it only sets `time_flag`/`fix_ok`).

---

## 11. Memory Budget

| Allocation | Size | Notes |
|---|---|---|
| PIO UART RX buffers (3×) | 1.5 KB | 512 B each |
| Line assembly buffers (3×) | 600 B | 200 B each |
| Detection queues (3×) | ~28 KB | W24/W5G 12 KB total + BLE ~16 KB |
| WiFi dedup tables (2×) | 26 KB | 512 × 52 B (compiled; W24+W5G share sizing) |
| BLE dedup table | 64 KB | 1024 × 64 B (`blebt_branch_v1_0.md` §14) |
| SD RAM ring | 32 KB | burst absorber for write stalls |
| FatFs work area + sector buffer | ~8 KB | |
| GPS NMEA buffer + position state | 1 KB | |
| Time state | 32 B | |
| Leaf state (3×) | 192 B | |
| Stacks (Core 0 + Core 1) | 8 KB | |
| USB CDC + UART0 buffers | 2 KB | |
| **Total** | **~172 KB** | Out of 264 KB SRAM |

Headroom: ~92 KB. BLE dedup dominates, as in the BLE Branch. No memory pressure, but tighter than any single Branch because three families coexist — the SD ring and BLE table are the two knobs if headroom is needed.

---

## 12. Build Configuration

### 12.1 Toolchain

RP2040 C SDK + CMake. PIO0 SM0–SM2 = RX, PIO1 SM0–SM2 = TX (`branch_controller_wifi24_v1_1_amendment.md` §12.1; the F-001 TX-retarget fix applies — `PINCTRL.OUT_BASE`/`SIDESET_BASE` updated per link). Three RX + three TX state machines fit the eight available.

### 12.2 Build Flags

```cmake
target_compile_definitions(agg_lite PRIVATE
    AGG_LITE_FW_VERSION="1.0.0"
    AGG_W24_SCANHOP=1            # expects WiFi24 Leaf scan-hop mode (see §15)
    AGG_BLE_BT_CLASSIC=0         # BT Classic deferred in light-duty
)
```

`AGG_LITE_FW_VERSION` lives in `include/agg_defs.h` and the CMake define, mirrored in this spec's flag example — one version string, three places, kept in lockstep (`CODE_STATUS.md` convention).

### 12.3 CI

Add `.github/workflows/agg_lite.yml`: `cmake -DPICO_BOARD=pico .. && make -j` → upload `agg_lite.uf2`, on push to `main` and PRs, matching the other RP2040 trees.

---

## 13. Testing Procedure

### 13.1 GPS-only

Flash, no Leaves. Verify `$LA` on the CDC mirror every 10 s; confirm `time_valid`→1 and `fix_ok`→1 with a pre-configured module; confirm no session file is created before the first fix, then a `YYYYMMDD_HHMMSS` directory appears at fix.

### 13.2 + 1 Leaf each (incremental)

Attach W24, confirm `$WA` on SD with `branch_id=W24`, `time_flag=0`, plausible `$AP`→`$WA` dedup. Repeat for W5_1 (`branch_id=W5G`) and BLE-1 (`$BD`, `company_id` correct on a known beacon e.g. Flock `2504`).

### 13.3 Standalone SD

No USB host. Walk a dense environment 60 s; pull the card; confirm `records.log` is well-formed, `f_sync`'d, no truncated final line, `$LA.err_count` reflects any ring drops.

### 13.4 Full 3-Leaf

All three Leaves; confirm interleaved `$WA`(W24)/`$WA`(W5G)/`$BD` with monotonic timestamps and that the SD ring high-water stays below capacity under burst.

### 13.5 PPS loss / SD hot-remove

Disconnect 1PPS: records continue with `time_flag=1`, `$LA.pps_age_ms` climbs. Hot-remove SD: `FR_DISK_ERR` handled, re-mount on reinsert, `sd_ok` tracks.

---

## 14. Open Items

| # | Item | Status |
|---|------|--------|
| 1 | GPS transport: UART1 (this spec) vs I2C (STM32 baseline). Decide before it hardens across versions. | Decision pending |
| 2 | 2.4 GHz WIDS in single-Leaf config: add a 2nd W24 Leaf, or derive `$WA` + WIDS from a promiscuous-only Leaf (`$BC`/`$DE`/`$PR` → `$WA`/`$ET`/`$DF`). | Deferred; target v1.1 |
| 3 | 5 GHz coverage tradeoff: single W5_1 (UNII-1+2A) vs channel-set 7 (all, ~2.5 s sweep) vs adding W5_2. | Config choice; documented |
| 4 | BT Classic deferred (`leaf_bt_classic`/`$BC_T` omitted). | Deferred; target v1.1 |
| 5 | SPI-FatFs library selection + SD write-stall headroom (ring depth vs card class). | Implementation phase |
| 6 | GPS pre-config `gps_push_config()` no-op; module pre-configured via u-center. | Placeholder; target v1.1 |
| 7 | WiFi24 Leaf scan-hop mode — depends on the Leaf-side amendment (§15). | Blocks W24 single-Leaf coverage |
| 8 | Power budget: RP2040 + 3 Leaves + GPS + SD total draw and supply (cf. payload-doc 3.3 V feed, ~1.3 W). | Not yet analyzed |
| 9 | Final GPIO assignments pending PCB layout. | Preliminary in §1 |
| 10 | Dedup window tuning per family at the merged ingest rate. | Needs field testing |

---

## 15. Cross-References & Leaf-Side Prerequisites

- `branch_controller_wifi24_v1_0.md` + v1.1/v1.2 amendments — the BC architecture this tree reuses (Core split, dedup, PPS/`$TM`, PIO, F-001/F-004 fixes).
- `wifi5_branch_v1_0.md` — W5G Leaf + BC patterns; missing-Leaf tolerance.
- `blebt_branch_v1_0.md` — BLE Leaf protocol (`$BL`/`$BX`), BLE dedup, `$BD`/`$BX` upstream schema.
- `stm32_h753_firmware_v1_0.md` §4, §8 — GPS NMEA and SD-logging logic ported here; operating-mode model.
- `system_plan_v2.md` + amendments — record schema and Analyzer compatibility.

**Leaf-side prerequisites (separate documents, not this spec):**
1. **WiFi24 Leaf scan-hop mode** — a `wifi24_leaf_protocol` amendment adding a scan mode that rotates ch 1/6/11 (today's scan parks; WIDS hops promiscuously). Required for §6.1. Version-bump `leaf_wifi24` accordingly.
2. **`leaf_ble`** — the BLE-only Leaf firmware (`blebt_branch_v1_0.md` §7) remains unbuilt; this variant consumes it. BT Classic (`leaf_bt_classic`) not required for light-duty.
3. **`leaf_wifi5`** — usable as-is for a single W5_1 in scan mode; channel-set is a `$CF` parameter.
