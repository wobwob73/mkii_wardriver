# FPV Detection Branch — Leaf Protocol & BC Firmware Guide

**Version:** 1.0.0
**Date:** 2026-05-29
**Scope:** Leaf protocol + RP2040 Branch Controller firmware for the FPV Detection Branch
**Target Hardware:** 4 Leaves — 2× Arduino Nano + RX5808 (5.8 GHz video scan), 1× ESP32-S3 + SX1280 (2.4 GHz ELRS C2), 1× Arduino Nano + SX1262 (433 MHz legacy C2) — + RP2040 Branch Controller
**Dependencies:** Patterns from prior Branch specs. Reuses RX5808 SPI control patterns (rotorhazard / FrSky FrSkyMavlinkProto) and SX128x/SX126x driver patterns from the Meshtastic and UHF ISM Branches.

---

## 1. Branch Topology

```
RP2040 Branch Controller
├── PIO0 SM0 (GP0 TX, GP1 RX) ── Leaf FPV-58-A   (Nano + RX5808, 5.8 GHz scanner #1)
├── PIO0 SM1 (GP2 TX, GP3 RX) ── Leaf FPV-58-B   (Nano + RX5808, 5.8 GHz scanner #2, diversity)
├── PIO0 SM2 (GP4 TX, GP5 RX) ── Leaf FPV-LORA-24 (ESP32-S3 + SX1280, 2.4 GHz LoRa C2)
├── PIO0 SM3 (GP6 TX, GP7 RX) ── Leaf FPV-LORA-433 (Nano + SX1262, 433 MHz legacy C2)
├── PIO1 SM0                    TX (OUT-pin remap)
├── UART0 (GP12 TX, GP13 RX) ── STM32 #2 UART5 (upstream)
└── 1PPS (GP10 EXTI) ─────────── from STM32 #2 GPS
```

4 Leaves — standard 4-SM PIO0 + 1-SM PIO1 (TX) pattern. No UART1 overflow needed.

---

## 2. Per-Leaf Configuration

| Leaf | Radio module | Frequency / band | Role |
|---|---|---|---|
| FPV-58-A | Arduino Nano + RX5808 (SPI-modded) | 5.645–5.945 GHz (48 FPV channels) | Continuous channel sweep, RSSI per channel, active-transmitter detection |
| FPV-58-B | Arduino Nano + RX5808 (SPI-modded) | Same | Diversity twin — confirms detections and reduces false positives from noise spikes |
| FPV-LORA-24 | ESP32-S3 + SX1280 | 2.4 GHz LoRa (ISM 2400.0–2483.5 MHz) | ELRS / Crossfire 2.4 GHz C2 link detection (LoRa modulation with characteristic SF/BW combos) |
| FPV-LORA-433 | Arduino Nano + SX1262 | 433 MHz | Legacy UHF C2 detection — DragonLink, OpenLRS, Frsky LBT |

### 2.1 RX5808 SPI-Modded Requirement

Stock RX5808 modules ship with parallel A0-A2 channel-select inputs. The **SPI mod** unsolders one resistor and bridges three pads to expose the internal SPI control bus (CLK, DATA, LE pins on the module). This is required because (a) parallel mode only addresses 25 channels and (b) SPI mode allows arbitrary tuning across the full 5.645–5.945 GHz range needed for the 48-channel map.

HANDOFF.md §8 records this as a v1.0 hardware requirement: AKK Diversity Receiver is unsuitable; **RotorHazard-compatible RX5808 modules** are the quality signal.

### 2.2 5.8 GHz Channel Map (48 channels)

| Band | Channels | Frequencies (MHz) |
|---|---|---|
| Boscam A | A1–A8 | 5865, 5845, 5825, 5805, 5785, 5765, 5745, 5725 |
| Boscam B | B1–B8 | 5733, 5752, 5771, 5790, 5809, 5828, 5847, 5866 |
| Boscam E | E1–E8 | 5705, 5685, 5665, 5645, 5885, 5905, 5925, 5945 |
| Fatshark | F1–F8 | 5740, 5760, 5780, 5800, 5820, 5840, 5860, 5880 |
| Raceband | R1–R8 | 5658, 5695, 5732, 5769, 5806, 5843, 5880, 5917 |
| Lowband | L1–L8 | 5333, 5373, 5413, 5453, 5493, 5533, 5573, 5613 (rarely used, optional) |

Total: 40 standard + 8 lowband. RX5808 supports all of these via SPI.

**Sweep cadence:** RX5808 settles to a new channel in ~25 ms. A 48-channel sweep takes ~1.2 s. The Leaf reports a `$FV` record per detected active channel at the end of each sweep.

### 2.3 SX1280 (2.4 GHz LoRa)

SX1280 is Semtech's 2.4 GHz LoRa transceiver — different chip from the SX1262 (sub-GHz). ESP32-S3 SPI control. RadioLib has SX1280 support.

ELRS 2.4 GHz configurations:
- 500 Hz mode: SF5, BW 800 kHz
- 250 Hz mode: SF5, BW 400 kHz
- 150 Hz mode: SF6, BW 400 kHz
- 50 Hz mode: SF7, BW 250 kHz

The Leaf rotates through these 4 presets every 200 ms (~50 ms/preset) to catch each rate.

---

## 3. Framing

Identical to `wifi24_leaf_protocol_v1_1.md` §2.

---

## 4. Upstream Messages (Leaf → RP2040)

Three message types, one per Leaf class.

### 4.1 `$FV` — FPV Video Channel Detection (from FPV-58-A/B)

Emitted at the end of each ~1.2 s RX5808 sweep, one record per active channel (RSSI above noise-floor threshold).

```
$FV,leaf_id,freq_khz,band,channel_name,rssi_raw,rssi_pct,active,first_seen_ms,last_seen_ms,time_flag*XX\n
```

| Field | Type | Description |
|---|---|---|
| leaf_id | string | `FPV-58-A` or `FPV-58-B` |
| freq_khz | int | Center frequency in kHz |
| band | int | 1=Boscam A, 2=Boscam B, 3=Boscam E, 4=Fatshark, 5=Raceband, 6=Lowband |
| channel_name | string | E.g., `R1`, `F4`, `A7` (band letter + 1-based index) |
| rssi_raw | int | RX5808 analog AGC ADC reading 0–1023 |
| rssi_pct | int | Calibrated 0–100% (per per-band calibration table compiled into the Leaf) |
| active | int | 1 if `rssi_raw > NOISE_FLOOR_THRESHOLD` (default 350; tunable via `$CF`) |
| first_seen_ms | int | millis() of first detection on this channel within current sweep window |
| last_seen_ms | int | millis() of latest sample |
| time_flag | int | 0/1 |

**Field count:** 11.

Only `active==1` channels are emitted (silent channels are not reported). Sweep cadence: one `$FV` burst per channel per ~1.2 s; the BC's `$VA` aggregator (§9.1) further reduces noise across A/B diversity.

### 4.2 `$LR` — LoRa C2 Packet Detection (from FPV-LORA-24 and FPV-LORA-433)

Reuses the `$LR` format from `meshtastic_branch_v1_0.md` §4.1. The `proto_hint` enum extends with:

| Value | Meaning |
|---|---|
| 5 | ELRS 2.4 (from FPV-LORA-24) — SF5/6 + BW400/800 detection |
| 6 | Crossfire 2.4 (from FPV-LORA-24) |
| 8 | DragonLink (from FPV-LORA-433) |
| 9 | OpenLRS (from FPV-LORA-433) |
| 10 | Frsky LBT (from FPV-LORA-433) |

Sub-fields `node_id_hex`, `hop_count`, `chan_hash_hex` may be `0` for ELRS/Crossfire — these protocols don't carry mesh-style routing metadata. The fields are populated when extractable from packet headers.

### 4.3 `$HB`

Same shape as WiFi24 v1.1 §3.6. For FPV-58 Leaves, `scan_count` = completed 48-channel sweeps.

---

## 5. Downstream Messages (RP2040 → Leaf)

### 5.1 `$CF` — Configuration (Per Leaf Class)

#### FPV-58-A/B (RX5808 Scanner)
```
$CF,leaf_id,channel_mask_hex,dwell_ms,noise_floor,reserved*XX\n
```

| Field | Description |
|---|---|
| channel_mask_hex | 64-bit mask (16 hex chars), bit i = channel i in the canonical 48-channel order (Boscam A first, then B, E, Fatshark, Raceband, Lowband). Default all-on = `FFFFFFFFFFFFFFFF`. |
| dwell_ms | Per-channel dwell time (default 25) |
| noise_floor | RSSI threshold (raw ADC), default 350 |

#### FPV-LORA-24 (SX1280)
```
$CF,leaf_id,sweep_mode,reserved*XX\n
```

| Field | Description |
|---|---|
| sweep_mode | 0 = ELRS rate sweep, 1 = SF5/BW800 fixed (race), 2 = SF6/BW400 fixed (long-range), 3 = SF7/BW250 fixed (50 Hz) |

#### FPV-LORA-433 (SX1262)
Same shape as the Meshtastic Leaf §5.1 — `freq_khz`, `sf`, `bw_khz`, `cr_id`, `sync_word`.

### 5.2 `$CH` — Not Used

For RX5808 Leaves, channel selection is handled via `$CF`'s `channel_mask_hex`. The SX1280 and SX1262 Leaves don't have a meaningful channel-mask concept.

### 5.3 `$PG`, `$RB`

Same as WiFi24 v1.1 §4.3, §4.4.

---

## 6. Boot Sequence

Identical pattern to WiFi24 v1.1 §5. Default `$CF`s:
- `$CF,FPV-58-A,FFFFFFFFFFFFFFFF,25,350,0*XX`
- `$CF,FPV-58-B,FFFFFFFFFFFFFFFF,25,350,0*XX`
- `$CF,FPV-LORA-24,0,0*XX` (ELRS rate sweep)
- `$CF,FPV-LORA-433,433175,11,125,1,2B,0*XX` (default LongFast — adjust to known DragonLink configuration when available)

---

## 7. Firmware Architecture — Leaves

### 7.1 FPV-58 Leaves (RX5808 Scanner)

#### Library
RotorHazard's open-source RX5808 driver for Arduino is the canonical reference: `https://github.com/RotorHazard/RotorHazard/blob/main/src/node/rhnode/RX5808.cpp` (BSD-licensed; vendor verbatim with attribution).

#### State Machine

```
[BOOT] → [WAIT_CONFIG] → [CONFIGURE] → [SWEEP]
                                          │
                          for each ch in mask:
                              RX5808.set_frequency(freq)
                              delay(dwell_ms - settling)
                              rssi = analogRead(RSSI_PIN)
                              if rssi > noise_floor:
                                  record_active(ch, rssi)
                                          │
                          end of sweep → emit_FV_for_each_active()
                          → $HB if due
                                          ↓
                                       [SWEEP]
```

#### Pin Map

| Function | Pin |
|---|---|
| RX5808 SPI CLK | D2 |
| RX5808 SPI DATA | D3 |
| RX5808 SPI LE (latch enable) | D4 |
| RX5808 RSSI analog | A0 |
| UART TX → BC | D1 |
| UART RX ← BC | D0 |

#### RSSI Calibration

The RX5808 analog AGC output is approximately linear in dB over a useful range (~-90 dBm to -30 dBm) but has band-to-band variation. The v1.0 firmware uses **per-band linear calibration** stored as compile-time constants. The values come from RotorHazard's published calibration curves; the analyzer can apply further corrections.

`rssi_pct` is a UI-friendly 0–100% scale (linear within band). `rssi_raw` is the unprocessed ADC reading for analyzer-side recalibration.

### 7.2 FPV-LORA-24 Leaf (SX1280)

ESP32-S3 + SX1280 via SPI. RadioLib supports SX1280:

```cpp
SX1280 radio = new Module(NSS, DIO1, RST, BUSY);
radio.begin(
    freq_mhz=2440.0,
    bw_khz=812.5,    // SX1280's 800 kHz approximation
    sf=5,
    cr_denom=5,
    sync_word=0x44,  // ELRS public sync word; varies
    output_power=0,
    preamble_length=12
);
radio.setDio1Action(rx_done_isr);
radio.startReceive();
```

Sweep mode (default): rotate through 4 ELRS configurations every 200 ms each. Total cycle 800 ms.

Capture-and-classify flow identical to the Meshtastic Leaf (§7.4 of that spec).

### 7.3 FPV-LORA-433 Leaf (SX1262)

Identical to the Meshtastic 433 MHz Leaf (`leaf_meshtastic` with `LORA_DEFAULT_FREQ_KHZ=433175`) but with a `proto_classify_legacy_c2.cpp` module that recognizes DragonLink / OpenLRS / Frsky LBT framing.

---

## 8. RP2040 Branch Controller — Architecture

Same dual-core pattern as `branch_controller_wifi24_v1_0.md` §2. PIO0 = 4 RX SMs, PIO1 = 1 TX SM.

### 8.1 Branch ID

`FPV` in all upstream messages.

### 8.2 Aggregation Tables

| Table | Source | Window | Capacity |
|---|---|---|---|
| `agg_video` | `$FV` from FPV-58-A/B | 5 s | 48 (one slot per channel) |
| `dedup_lora` | `$LR` from FPV-LORA-24, FPV-LORA-433 | 60 s | 64 entries |

`agg_video` is keyed by `(freq_khz, band)` and persistently tracks "video channel X has been active for N seconds, first_seen at T1, last_seen at T2". Emitted upstream as `$VA` (video aggregate) when:
- a channel transitions from `active=0 → 1` (new transmitter appeared), or
- a channel has been continuously active for N seconds (periodic re-emit every 10 s), or
- a channel transitions `active=1 → 0` (transmitter dropped — useful for tracking handoffs).

`dedup_lora` reuses the Meshtastic Branch's dedup logic for LoRa C2.

### 8.3 UAS Probable-Drone Correlation (Phase-1 Marker)

If a video channel is active AND a LoRa C2 packet is observed within the same 5 s window from the same general direction (assumed from RSSI on diverse Leaves), the BC sets a "uas_likely" hint in the next `$VA` and `$LA`. The actual drone-classification logic lives on the analyzer; the BC only surfaces the temporal coincidence.

For v1.0 the hint is a coarse: `uas_hint = 1` if (any `agg_video.active == 1`) AND (any `dedup_lora` entry seen in last 5 s). Refinement (direction-of-arrival, signal-strength correlation) is Phase 1+ of analyzer development.

---

## 9. Upstream Messages (RP2040 → STM32)

### 9.1 `$VA` — Aggregated FPV Video Channel

```
$VA,FPV,timestamp,freq_khz,band,channel_name,rssi_raw_best,rssi_pct_best,leaf_id_best,event,active_duration_ms,uas_hint,time_flag*XX\n
```

| Field | Description |
|---|---|
| event | 0 = active-now (periodic), 1 = new active, 2 = dropped (just went inactive), 3 = re-emerged |
| active_duration_ms | Cumulative ms this channel has been active in the current activity window |
| uas_hint | Per §8.3 |

**Field count:** 13.

### 9.2 `$LA` — Aggregated LoRa C2 Detection

Same shape as `meshtastic_branch_v1_0.md` §9.1 with `branch_id = FPV` and the extended `proto_hint` enum.

**Field count:** 18.

### 9.3 `$BS` — Branch Status

```
$BS,FPV,uptime_s,time_valid,fix_ok,pps_age_ms,fpv58a_st,fpv58b_st,lora24_st,lora433_st,active_channels,dedup_lora_count,err_count*XX\n
```

**Field count:** 13.

---

## 10. Downstream Messages (STM32 → RP2040)

`$TM`, `$RC`, `$RQ` per BC v1.0 §8. `$RC` `leaf_id` namespace: `FPV-58-A/B`, `FPV-LORA-24`, `FPV-LORA-433`.

---

## 11. Module Decomposition

```
branch_fpv/
├── CMakeLists.txt
├── pico_sdk_import.cmake
├── README.md
├── include/branch_defs.h
├── pio/
│   ├── uart_rx.pio
│   └── uart_tx.pio
└── src/
    ├── main.c
    ├── core0_leaf_io.{h,c}
    ├── core1_upstream.{h,c}
    ├── pio_uart.{h,c}
    ├── proto.{h,c}
    ├── queues.{h,c}
    ├── pps_time.{h,c}
    ├── leaf_cmd.{h,c}
    ├── leaf_health.{h,c}
    ├── agg_video.{h,c}        48-slot per-channel state machine
    ├── dedup_lora.{h,c}       same shape as Meshtastic Branch
    ├── uas_corr.{h,c}         §8.3 coincidence detection
    └── upstream_fmt.{h,c}     $VA / $LA / $BS

leaf_fpv_58/             Arduino, RX5808 SPI sweep + RotorHazard driver
leaf_fpv_lora_24/        Arduino on ESP32-S3, SX1280 + ELRS preset rotation
leaf_fpv_lora_433/       Arduino, SX1262 + legacy C2 classifier (DragonLink/OpenLRS/Frsky LBT)
```

Three Leaf binaries.

---

## 12. State Machine — Branch Controller

Same as `branch_controller_wifi24_v1_0.md` §10. `agg_video` is initialized to "all 48 channels inactive" at boot; first `$FV` from either FPV-58 Leaf transitions slots to `active=1`.

---

## 13. Memory Budget

| Allocation | Size | Notes |
|---|---|---|
| PIO UART RX buffers (4×) | 2 KB | 512 B each |
| Line assembly buffers (4×) | 800 B | |
| `agg_video` | 2 KB | 48 × ~40 B (freq + RSSI history + state) |
| LoRa C2 Queue | 4 KB | 32 × 128 B |
| `dedup_lora` | 16 KB | 64 × 256 B |
| `uas_corr` state | 256 B | tiny coincidence detector |
| Stacks | 8 KB | |
| UART0 buffers | 1 KB | |
| **Total** | **~34 KB** | Of 264 KB RP2040 SRAM |

Headroom: ~230 KB.

---

## 14. Build Configuration

### 14.1 RP2040 BC

CMake per existing pattern, executable `branch_fpv`.

### 14.2 FPV-58 Leaf

```ini
[env:leaf_fpv_58]
platform = atmelavr
board = nanoatmega328new
framework = arduino
monitor_speed = 230400
build_flags =
    -DLEAF_VERSION="1.0.0"
    -DLEAF_KIND_FPV58=1
```

(RotorHazard RX5808 driver vendored into `lib/RX5808/`.)

### 14.3 FPV-LORA-24 Leaf

```ini
[env:leaf_fpv_lora_24]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 230400
lib_deps = jgromes/RadioLib @ ^6.6.0
build_flags =
    -DLEAF_VERSION="1.0.0"
    -DLEAF_KIND_FPV_LORA24=1
```

### 14.4 FPV-LORA-433 Leaf

```ini
[env:leaf_fpv_lora_433]
platform = atmelavr
board = nanoatmega328new
framework = arduino
monitor_speed = 230400
lib_deps = jgromes/RadioLib @ ^6.6.0
build_flags =
    -DLEAF_VERSION="1.0.0"
    -DLEAF_KIND_FPV_LORA433=1
```

---

## 15. Testing Procedure

### 15.1 FPV-58 Single Leaf

1. Power a known FPV transmitter (e.g., a TBS Unify or similar) on Raceband R7.
2. Verify `$FV` arrives within one sweep (~1.2 s) with `band=5`, `channel_name=R7`, `active=1`.
3. Walk away: `rssi_raw` decreases; verify the channel transitions to `active=0` when below `noise_floor`.

### 15.2 Diversity Validation

1. Run FPV-58-A and FPV-58-B with antennas in different orientations.
2. Verify `$VA` only emits `event=1` once when both Leaves see the transmitter (BC suppression).
3. Verify which Leaf has the better RSSI is captured in `leaf_id_best`.

### 15.3 ELRS 2.4 Detection

1. Power an ELRS 2.4 GHz quad TX in 500 Hz mode.
2. Verify `$LR` with `proto_hint=5` arrives from FPV-LORA-24 within seconds.

### 15.4 Legacy 433 C2

1. Power a DragonLink-equipped legacy fixed-wing TX.
2. Verify `$LR` with `proto_hint=8` from FPV-LORA-433.

### 15.5 UAS Correlation

1. Power both an FPV video TX (R7) and an ELRS 2.4 link simultaneously.
2. Verify `$VA` and `$LA` both carry `uas_hint=1` within the 5 s correlation window.

### 15.6 BC + STM32 #2

End-to-end via STM32 #2 UART5.

---

## 16. Open Items

| # | Item | Status |
|---|---|---|
| 1 | RX5808 sourcing — RotorHazard-compatible SPI-modded units; quality varies wildly | Pending sourcing |
| 2 | 1.3 GHz analog FPV detection — done at Trunk via RTL-SDR, not this Branch | Documented |
| 3 | Digital FPV (DJI O3 / HDZero / Walksnail / Avatar) detection — energy detection on 5.8 GHz works (RX5808 sees a carrier) but signal-type discrimination requires SDR analysis | Trunk-side enhancement |
| 4 | UAS correlation — Phase-1 spatial fix (direction-of-arrival from RSSI gradient) requires more than two RX5808s; defer | Future v1.1 |
| 5 | ELRS sync-word evolution — public ELRS firmware bumps sync words occasionally; track upstream | Living document |
| 6 | RSSI calibration: per-RX5808 unit variability (5–10 dB) | Per-unit cal at production |
| 7 | Antennas: 5.8 GHz omnidirectional + 2.4 GHz omnidirectional + 433 MHz whip | Hardware sourcing |
| 8 | Power: 2 Nano + 1 S3 + 1 Nano + 2 RX5808 + SX1280 + SX1262 ≈ 180 mA peak | OK on USB rail |

---

## 17. Cross-References

- `system_plan_v2.md` §3.6 — FPV Detection Branch detail.
- `system_plan_v2_1_amendment.md` §3.2 — STM32 #2 Branch allocation (UART5).
- `stm32_h753_firmware_v1_0.md` §1.3 — STM32 #2 pin map.
- `meshtastic_branch_v1_0.md` §4.1 — base `$LR` format and proto_hint enum (extended here).
- `uhf_ism_branch_v1_0.md` — additional `proto_hint` extensions for LoRa C2 patterns.
- `branch_controller_wifi24_v1_0.md` §10 — BC state-machine template.
- HANDOFF.md §3 — `fpv_detections` and `lora_detections` table targets.
- HANDOFF.md §8 — UAS correlation (LoRa C2 + FPV video).
