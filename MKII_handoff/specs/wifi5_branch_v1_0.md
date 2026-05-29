# 5 GHz WiFi Branch — Leaf Protocol & BC Firmware Guide

**Version:** 1.0.0
**Date:** 2026-05-29
**Scope:** Leaf protocol + RP2040 Branch Controller firmware for the 5 GHz WiFi Branch
**Target Hardware:** ESP32-C5 Leaves (×3, scan + WIDS), RP2040 Branch Controller
**Dependencies:** Patterns established by `wifi24_leaf_protocol_v1_1.md` + v1.2 amendment and `branch_controller_wifi24_v1_0.md` + v1.1 amendment. This doc states differences from WiFi24 and the 5 GHz–specific data tables; shared infrastructure (NMEA framing, `$HB`/`$CF`/`$CH`/`$PG`/`$RB`, SPSC pattern, tombstone dedup, PIO0=RX/PIO1=TX, PPS + `$TM`) is referenced by section.

---

## 1. Branch Topology

```
RP2040 Branch Controller
├── PIO0 SM0 (GP0 TX, GP1 RX) ── Leaf W5_1 (Scan, UNII-1 channels 36–48)
├── PIO0 SM1 (GP2 TX, GP3 RX) ── Leaf W5_2 (Scan, UNII-3 channels 149–165)
├── PIO0 SM2 (GP4 TX, GP5 RX) ── Leaf W5_3 (WIDS, hop all 5 GHz channels)
├── PIO0 SM3 ────────────────── (unused; reserved for optional W5_4 future)
├── UART0 (GP12 TX, GP13 RX) ── STM32 #1 USART2 (upstream)
└── 1PPS (GP10 EXTI) ─────────── from STM32 #1 GPS
```

**Baud rate:** 230 400 8N1 on every link. PIO TX uses the single-SM-with-pin-remap pattern from `branch_controller_wifi24_v1_1_amendment.md` §12.1.

**Leaves per the System Plan v2.1 §3.1:** 2–3 ESP32-C5 modules. The minimum useful configuration is 2 (W5_1 + W5_2). W5_3 (WIDS) is recommended; the RP2040 firmware tolerates W5_3 absent.

**Boards:** Seeed XIAO ESP32-C5 (factory U.FL) once available, or DevKit-C5 reference module. Single binary, identity adopted from first `$CF` (same pattern as WiFi24 Leaf §4.1).

---

## 2. 5 GHz Channel Map

The 5 GHz US channels of interest, grouped by UNII band. Channel widths in deployed APs are 20/40/80/160 MHz; beacons are emitted on the AP's primary 20 MHz subchannel at a low data rate, so a single 20 MHz–wide Leaf can detect 80/160 MHz APs by parking on representative primary channels.

| UNII band | Channels (20 MHz primaries) | Notes |
|---|---|---|
| UNII-1 | 36, 40, 44, 48 | No DFS. Common consumer APs default here. |
| UNII-2A | 52, 56, 60, 64 | DFS required for TX; passive RX unrestricted. |
| UNII-2C | 100, 104, …, 144 | DFS. Less common; weather radar coexistence. |
| UNII-3 | 149, 153, 157, 161, 165 | No DFS. Densely populated US consumer band. |

### 2.1 Per-Leaf Channel Assignment

| Leaf | Role | Channel set | Cadence |
|---|---|---|---|
| W5_1 | Scan | 36, 40, 44, 48, 52, 56, 60, 64 (UNII-1 + UNII-2A) | passive scan, hop every ~200 ms |
| W5_2 | Scan | 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165 (UNII-2C + UNII-3) | passive scan, hop every ~200 ms |
| W5_3 | WIDS | all 25 channels, hop with default dwell 100 ms | promiscuous, beacons/probes/deauths |

Hopping is mandatory in 5 GHz (unlike 2.4 GHz where 1/6/11 cover the band via spectral overlap — 5 GHz channels don't overlap, each AP is on its own channel). Full sweep time:

- W5_1: 8 channels × 200 ms ≈ 1.6 s per sweep.
- W5_2: 17 channels × 200 ms ≈ 3.4 s per sweep.
- W5_3 (WIDS): 25 channels × 100 ms = 2.5 s per sweep.

These cadences are slower than WiFi24 (~1.4 s for the W4 WIDS Leaf on 14 channels) — the analyzer should not assume `$WA` arrival rate matches WiFi24.

### 2.2 Listen-Only Compliance

DFS rules govern transmit; receive-only operation on DFS channels is permitted. The ESP32-C5 must be configured for **passive scan** (no probe requests) per `wifi24_leaf_protocol_v1_2_amendment.md` §6.4 to remain listen-only.

---

## 3. Framing

Identical to `wifi24_leaf_protocol_v1_1.md` §2 (NMEA-style `$..*XX\n`, 230 400 baud, 200-byte max line, XOR checksum, hex-encoded SSID). No 5 GHz–specific framing changes.

---

## 4. Upstream Messages (Leaf → RP2040)

All message types match WiFi24 Leaf protocol v1.1 + v1.2 amendment. The `enc` field accepts the full 0–10 range from v1.2 §3.1.1.

### 4.1 `$AP` — WiFi AP Detection (5 GHz)

Format identical to WiFi24 v1.1 §3.1:

```
$AP,leaf_id,BSSID,SSID_hex,RSSI,channel,enc,hidden*XX\n
```

| Field | Difference vs WiFi24 |
|---|---|
| leaf_id | `W5_1`, `W5_2`, `W5_3` |
| channel | 36–165, not 1–14 |
| All other fields | Identical |

**Field count:** 8 (unchanged).

### 4.2 `$BK`, `$DE`, `$PR`, `$BC`, `$HB`

Same shape as WiFi24, with `leaf_id` in the `W5_*` range. No semantic changes.

### 4.3 New for WiFi 6: HE/EHT Indicators (Future)

WiFi 6 (HE) and WiFi 6E/7 (EHT) introduce additional IEs (HE Capabilities tag 255 ext 35, EHT Capabilities tag 255 ext 108). v1.0 of this spec does **not** decode them — the `enc` field handles security; HE/EHT-specific surfacing would be an analyzer-side enrichment from raw beacon captures forwarded over `$BC`. A future v1.1 amendment may add a `phy_mode` field. v1.0 only signals "this is a 5 GHz AP" via `channel >= 36`.

---

## 5. Downstream Messages (RP2040 → Leaf)

### 5.1 `$CF` — Configuration (Updated)

```
$CF,leaf_id,mode,channel_set_id,dwell_ms,reserved*XX\n
```

Difference from WiFi24 v1.1 §4.1: instead of a single `channel` field, the Leaf takes a **channel-set ID** because 5 GHz scan Leaves cover multiple channels each.

| `channel_set_id` | Meaning |
|---|---|
| 0 | (unused; reserved) |
| 1 | UNII-1 only (36–48) |
| 2 | UNII-2A only (52–64) |
| 3 | UNII-1 + UNII-2A (default for W5_1) |
| 4 | UNII-2C only (100–144) |
| 5 | UNII-3 only (149–165) |
| 6 | UNII-2C + UNII-3 (default for W5_2) |
| 7 | All 5 GHz (default for W5_3 / WIDS) |

| Field | Type | Description |
|---|---|---|
| mode | int | 0 = scan, 1 = WIDS |
| channel_set_id | int | 1–7 per table above |
| dwell_ms | int | Per-channel dwell time. Scan: 200; WIDS: 100. |
| reserved | int | Send 0 |

**Field count:** 6 (unchanged structure; semantics shift).

### 5.2 `$CH` — Channel Mask Override

```
$CH,leaf_id,bitmask_lo,bitmask_hi*XX\n
```

5 GHz needs **32-bit** of bitmask coverage. Split into two 16-bit fields (`bitmask_lo` covers channels 36–64 indexed by position in the canonical 25-channel list; `bitmask_hi` covers channels 100–165). The Leaf reconstructs `mask = (bitmask_hi << 16) | bitmask_lo`.

Canonical channel index → bit position:

| Bit | Channel | UNII |
|---|---|---|
| 0 | 36 | 1 |
| 1 | 40 | 1 |
| 2 | 44 | 1 |
| 3 | 48 | 1 |
| 4 | 52 | 2A |
| 5 | 56 | 2A |
| 6 | 60 | 2A |
| 7 | 64 | 2A |
| 8 | 100 | 2C |
| 9 | 104 | 2C |
| 10 | 108 | 2C |
| 11 | 112 | 2C |
| 12 | 116 | 2C |
| 13 | 120 | 2C |
| 14 | 124 | 2C |
| 15 | 128 | 2C |
| 16 | 132 | 2C |
| 17 | 136 | 2C |
| 18 | 140 | 2C |
| 19 | 144 | 2C |
| 20 | 149 | 3 |
| 21 | 153 | 3 |
| 22 | 157 | 3 |
| 23 | 161 | 3 |
| 24 | 165 | 3 |
| 25–31 | (reserved) | |

**Field count:** 4.

### 5.3 `$PG`, `$RB`

Identical to WiFi24 v1.1 §4.3, §4.4.

---

## 6. Boot Sequence

Identical pattern to WiFi24 v1.1 §5. Substitute `$CF,W5_1,0,3,200,0`, `$CF,W5_2,0,6,200,0`, `$CF,W5_3,1,7,100,0`. Standalone fallback at 10 s timeout: scan mode, channel-set ID 3 (UNII-1+UNII-2A), under `leaf_id = W5_?`.

---

## 7. Firmware Architecture — Scan Leaf (W5_1, W5_2)

### 7.1 State Machine

Same as WiFi24 v1.1 §6.1 with one substitution: after applying `$CF`, the Leaf enters a `[SCANNING_HOP]` state that iterates its assigned channel set rather than parking on a single channel.

```
[CONFIGURE] → [SCANNING_HOP] ←──────────────┐
                  │                          │
        for each channel in set:             │
            start_async_scan(channel)        │
            wait_for_scan_done()             │
            emit $AP × N, $BK                │
        cycle complete → [IDLE_CHECK]        │
                  │                          │
        ($HB if due; process commands)       │
                  │                          │
                  └──────────────────────────┘
```

### 7.2 Implementation Notes (ESP-IDF/Arduino)

- ESP32-C5 support in ESP-IDF reached `v5.x` mainline as of 2026. Use `wifi_scan_config_t` with `scan_type = WIFI_SCAN_TYPE_PASSIVE`, `scan_time.passive = 200` ms per channel.
- The C5's `esp_wifi_set_band()` must be invoked once at boot for `WIFI_BAND_5G` before any scan. Dual-band scanning in a single call is not used — we want explicit 5 GHz coverage per Leaf.
- The async scan API on C5 is identical to C3; the firmware can lift `wifi_scan.cpp` from `leaf_wifi24` verbatim with the channel-iterator wrapper and band setup added.

### 7.3 Reused Modules from WiFi24

| Module | Reuse status |
|---|---|
| `uart_proto.{h,cpp}` | Verbatim |
| `config.{h,cpp}` | Modify `$CF` parsing for `channel_set_id`; otherwise verbatim |
| `heartbeat.{h,cpp}` | Verbatim |
| `cmd_handler.{h,cpp}` | Verbatim + new `$CH` 32-bit mask handler |
| `wifi_scan.{h,cpp}` | Verbatim + channel-set iterator + 5 GHz band setup |
| `wids_monitor.{h,cpp}` | Verbatim (channel hop list grown to 25 entries) |
| `frame_parser.{h,cpp}` | Verbatim — IE walker is band-agnostic |
| `bssid_tracker.{h,cpp}` | Verbatim |

The "single binary, multi-role" pattern from WiFi24 carries over: one firmware image, three Leaves, identity from `$CF`.

---

## 8. Firmware Architecture — WIDS Leaf (W5_3)

Identical structure to WiFi24 v1.1 §7. The only differences:

- 25 channels in the hop list vs 14.
- Channel set in `$CH` is the 32-bit mask defined in §5.2 above.
- Per-cycle sweep time: 2.5 s at 100 ms dwell.

The ring buffer (`WidsEvent`, 64 slots), seen-BSSID set (FNV-1a, 512 entries), beacon IE walker, and promiscuous-callback rules are unchanged.

---

## 9. RP2040 Branch Controller — Architecture

Same dual-core architecture as `branch_controller_wifi24_v1_0.md` §2:
- **Core 0:** PIO UART RX from up to 3 Leaves, line assembly, message dispatch, Leaf watchdog, downstream TX.
- **Core 1:** PPS ISR + `$TM` ingest + timestamp compute; AP dedup (500 ms window, tombstone reclaim); WIDS analysis (evil twin, deauth flood); upstream TX to STM32.

PIO usage matches `branch_controller_wifi24_v1_1_amendment.md` §12.1: PIO0 = 3 RX state machines (one unused), PIO1 = 1 TX with OUT-pin remap.

### 9.1 Differences from WiFi24 BC

| Aspect | WiFi24 BC | WiFi5 BC |
|---|---|---|
| Leaf count | 4 | 2 or 3 |
| PIO0 RX SMs | 4 | 2 or 3 (SM3 unused) |
| Dedup table size | 512 entries | 512 entries — same headroom suffices |
| Channel range in `DedupEntry.channel` | 1–14 (uint8_t) | 36–165 (uint8_t still fits) |
| Branch ID in upstream messages | `W24` | `W5G` |
| `$CH` field count for downstream relay | 3 | 4 (32-bit mask split) |

### 9.2 Branch ID

The 5 GHz Branch identifies itself as **`W5G`** in upstream `$WA`, `$WP`, `$ET`, `$DF`, `$BS` messages.

---

## 10. Upstream Messages (RP2040 → STM32)

Same shapes as `branch_controller_wifi24_v1_0.md` §7 with `branch_id = W5G`. Field counts unchanged.

### 10.1 `$WA` — Deduplicated AP

```
$WA,W5G,timestamp,BSSID,SSID_hex,RSSI,channel,enc,hidden,leaf_id,time_flag*XX\n
```

`channel` is the 5 GHz primary channel (36–165). `leaf_id` is `W5_1`, `W5_2`, or `W5_3`.

### 10.2 `$WP`, `$ET`, `$DF`, `$BS`

Identical formats with `W5G` as `branch_id`. The `$BS` Leaf-status fields shrink because we have at most 3 Leaves:

```
$BS,W5G,uptime_s,time_valid,fix_ok,pps_age_ms,w5_1_st,w5_2_st,w5_3_st,q_det_used,q_wids_used,dedup_count,err_count*XX\n
```

**Field count:** 13 (was 14 for WiFi24; one fewer Leaf status field). Absent W5_3: `w5_3_st = 0` (offline) is reported normally.

---

## 11. Downstream Messages (STM32 → RP2040)

Identical to `branch_controller_wifi24_v1_0.md` §8 with the `$RC` `leaf_id` namespace extended:

| Inner `leaf_id` | Routed to PIO SM |
|---|---|
| `W5_1` | PIO0 SM0 |
| `W5_2` | PIO0 SM1 |
| `W5_3` | PIO0 SM2 |
| Anything else | Drop, increment `err_count` |

`$TM` and `$RQ` are unchanged.

---

## 12. Module Decomposition

```
branch_wifi5/
├── CMakeLists.txt              pico-sdk + CMake
├── pico_sdk_import.cmake
├── README.md
├── include/
│   └── branch_defs.h           constants, structs, enums (W5G-specific)
├── pio/
│   ├── uart_rx.pio             same program as WiFi24
│   └── uart_tx.pio             same program as WiFi24
└── src/
    ├── main.c                  entry, core launch, init
    ├── core0_leaf_io.{h,c}     PIO UART mgmt, line assembly, dispatch (3 SMs)
    ├── core1_upstream.{h,c}    main loop, dedup flush, WIDS, upstream TX
    ├── pio_uart.{h,c}          PIO UART driver (shared pattern)
    ├── proto.{h,c}             framing, checksum, field parse, hex enc/dec
    ├── queues.{h,c}             SPSC ring buffers (true SPSC, __dmb)
    ├── pps_time.{h,c}           1PPS ISR, $TM parse, timestamp compute
    ├── leaf_cmd.{h,c}           downstream $CF/$CH/$PG/$RB construction
    ├── leaf_health.{h,c}        Leaf state tracking, watchdog, recovery
    ├── dedup.{h,c}              AP dedup hash + tombstone reclaim
    ├── wids.{h,c}               evil-twin + deauth-flood
    └── upstream_fmt.{h,c}       W5G upstream message formatting
```

Same skeleton as `branch_wifi24/` (29 files → ~27 here because 3 Leaves vs 4 trims a small amount of state). All shared modules can be lifted verbatim from the WiFi24 BC tree; `branch_defs.h`, `upstream_fmt.c`, and `cmd_handler.c` need 5 GHz–specific constants.

---

## 13. State Machine — Branch Controller

Identical to `branch_controller_wifi24_v1_0.md` §10:

```
[BOOT] → [WAIT_PPS] → [INIT_LEAVES] → [RUNNING]
```

`INIT_LEAVES` sends `$CF` to W5_1, W5_2, W5_3 with channel-set IDs 3, 6, 7 and the `mode` and `dwell_ms` defaults from §5.1. If only 2 Leaves are physically present, `W5_3` will time out after `cf_retries` and be marked offline; the BC continues with the 2 scan Leaves.

---

## 14. Memory Budget

| Allocation | Size | Notes |
|---|---|---|
| PIO UART RX buffers (3×) | 1.5 KB | 512 B each |
| Line assembly buffers (3×) | 600 B | 200 B each |
| Detection Queue | 12 KB | 256 × 48 B (unchanged) |
| WIDS Queue | 8 KB | 128 × 64 B (unchanged) |
| Dedup table | 26.5 KB | 512 × 53 B (incl. tombstone status byte) |
| Deauth tracker | 1 KB | |
| Leaf state (3×) | 192 B | |
| Time state | 32 B | |
| Stacks (Core 0 + Core 1) | 8 KB | |
| UART0 TX/RX | 1 KB | |
| **Total** | **~58 KB** | Out of 264 KB RP2040 SRAM |

Headroom: ~206 KB. The slightly smaller PIO RX footprint (one fewer Leaf) trades off against the same tombstone overhead in dedup.

---

## 15. Build Configuration

`platformio.ini` for the ESP32-C5 Leaf:

```ini
[env:leaf_wifi5]
platform = espressif32
board = esp32-c5-devkitc-1     ; or seeed XIAO C5 once mainlined
framework = espidf              ; arduino-esp32 C5 support pending; espidf is the safe baseline
monitor_speed = 230400
build_flags =
    -DLEAF_VERSION="1.0.0"
    -DLEAF_BAND_5G=1
    -DMAX_LINE_LEN=200
    -DHB_INTERVAL_MS=10000
    -DCONFIG_TIMEOUT_MS=10000
    -DSCAN_FAIL_REBOOT_THRESHOLD=50
    -DHEAP_MIN_BYTES=16384
    -DWIDS_RING_SIZE=64
    -DWIDS_SEEN_BSSID_MAX=512
```

ESP32-C5 Arduino-core support is pending in the espressif32 PlatformIO platform; building against ESP-IDF directly is the more reliable v1.0 baseline. The firmware code is C-compatible; once Arduino-core lands, the same source can be rebuilt with `framework = arduino` with minor `Serial`-vs-`uart_*` adapter changes.

`CMakeLists.txt` for the RP2040 BC follows `branch_wifi24/`'s structure exactly, with `branch_wifi5` as the executable name.

---

## 16. Testing Procedure

### 16.1 Bench Test — Single Leaf

Same as WiFi24 v1.1 §11.1 with adjustments:

1. Flash; verify `$HB` within 2 s.
2. Send `$CF,W5_1,0,3,200,0*XX` — verify Leaf hops UNII-1+2A and emits `$AP`/`$BK`.
3. Move to a known 5 GHz AP environment; verify channels 36–48 are captured.
4. Send `$CF,W5_1,1,7,100,0*XX` — verify WIDS mode hops all 25 channels.
5. Send `$CH,W5_1,15,0*XX` (UNII-1 only) — verify scan narrows.

### 16.2 BC + 3 Leaves

Same as WiFi24 v1.1 §11.2 but with 3 Leaves. In a dense 5 GHz environment:

- W5_1 (UNII-1+2A) typically sees 40% of APs in a US suburb (residential consumer APs on default channel 36/48).
- W5_2 (UNII-2C+3) sees ~60% (enterprise APs and high-density consumer).
- W5_3 (WIDS) sees probe/deauth activity on all bands.

Expected `$WA` rate at the STM32: 30–80 unique APs/s in dense urban; 5–15 in suburban.

### 16.3 PPS + STM32 Integration

Identical to `branch_controller_wifi24_v1_0.md` §13.4.

---

## 17. Open Items

| # | Item | Status |
|---|---|---|
| 1 | XIAO ESP32-C5 board availability + U.FL connector — confirm at sourcing | Pending |
| 2 | ESP-IDF v5.x C5 support level — verify passive scan API + 5 GHz band selection on the exact IDF tag chosen | Verify at first compile |
| 3 | DFS receive: ESP-IDF behavior when set to a UNII-2 channel without DFS-mode RX flag | Verify on hardware |
| 4 | HE/EHT IE decoding (WiFi 6 / 6E / 7 indicators) | Deferred to v1.1 |
| 5 | Final PCB pin assignments (preliminary in §1) | Pending |
| 6 | Power budget: RP2040 + 3× ESP32-C5 (C5 is more demanding than C3 in WiFi peak) | Not yet analyzed |
| 7 | Antenna: 5 GHz patch antenna selection per Leaf (Taoglas FXP840 family or similar) | Pending sourcing |
| 8 | Confirm whether 6 GHz (WiFi 6E) channels are reachable on the C5 SKU shipped — if yes, add a UNII-5/6/7/8 channel set ID to `$CF` | Future v1.1 / chip-revision-dependent |

---

## 18. Cross-References

- `wifi24_leaf_protocol_v1_1.md` + v1.2 amendment — the protocol family this Leaf inherits from.
- `branch_controller_wifi24_v1_0.md` + v1.1 amendment — the BC architecture this Branch reuses.
- `system_plan_v2_1_amendment.md` §3.1 — Branch slot allocation (STM32 #1 USART2).
- `stm32_h753_firmware_v1_0.md` §1.2 — upstream side pin map.
