# VHF ISM 315 / 433 MHz Branch — Leaf Protocol & BC Firmware Guide

**Version:** 1.0.0
**Date:** 2026-05-29
**Scope:** Leaf protocol + RP2040 Branch Controller firmware for the VHF ISM Branch (315 / 433 MHz ASK/OOK)
**Target Hardware:** 6× Arduino Nano (ATmega328P) + superheterodyne ASK/OOK receiver modules + RP2040 Branch Controller
**Dependencies:** Patterns from `wifi24_leaf_protocol_v1_1.md` + v1.2, `branch_controller_wifi24_v1_0.md` + v1.1. Shared infrastructure referenced by section.

---

## 1. Branch Topology

```
RP2040 Branch Controller
├── PIO0 SM0 (GP0 TX, GP1 RX) ── Leaf VHF-315-A (RXB12 @ 315 MHz, decoder set A)
├── PIO0 SM1 (GP2 TX, GP3 RX) ── Leaf VHF-315-B (RXB12 @ 315 MHz, decoder set B)
├── PIO0 SM2 (GP4 TX, GP5 RX) ── Leaf VHF-315-R (RXB12 @ 315 MHz, roaming protocols)
├── PIO0 SM3 (GP6 TX, GP7 RX) ── Leaf VHF-433-A (RXB6  @ 433 MHz, decoder set A)
├── PIO1 SM1 (GP8 TX, GP9 RX) ── Leaf VHF-433-B (RXB6  @ 433 MHz, decoder set B)
├── PIO1 SM2 (GP14 TX, GP15 RX)─ Leaf VHF-433-R (RXB6  @ 433 MHz, roaming protocols)
├── PIO1 SM0                    TX (OUT-pin remap across all 6 Leaves)
├── UART0 (GP12 TX, GP13 RX) ── STM32 #2 USART2 (upstream)
└── 1PPS (GP10 EXTI) ─────────── from STM32 #2 GPS
```

**PIO usage:** extends the Meshtastic Branch pattern to **6 Leaves** — PIO0 holds 4 RX SMs, PIO1 holds the shared TX SM (SM0) plus 2 RX SMs (SM1, SM2). PIO1's instruction memory still has room (RX ~8 + TX ~6 = 14 of 32 words used).

---

## 2. Per-Leaf Configuration

| Leaf | RX module | Frequency | Decoder profile | Role |
|---|---|---|---|---|
| VHF-315-A | RXB12 | 315.000 MHz | Garage door (PT2262/PT2264/HT12E) + Chamberlain | Common North-American garage codes |
| VHF-315-B | RXB12 | 315.000 MHz | Same as A; antenna-diversity twin | Diversity capture for weak signals |
| VHF-315-R | RXB12 | 315.000 MHz | Generic ASK/OOK pulse logger, any pattern | Catches unrecognized codes — raw bits forwarded |
| VHF-433-A | RXB6  | 433.920 MHz | rc-switch family (KAKU, Intertechno, EnerLogic) + Acurite weather + LaCrosse weather | Most common ISM IoT/sensor protocols |
| VHF-433-B | RXB6  | 433.920 MHz | Same as A; antenna-diversity twin | Diversity capture |
| VHF-433-R | RXB6  | 433.920 MHz | Generic ASK/OOK pulse logger | Catches unrecognized codes |

**Frequency note:** the RX modules are crystal-fixed to a single frequency. "Roaming" in the Leaf name refers to the firmware *decoder profile* (try many protocols), not RF frequency hopping. RF coverage is band-wide because superhet RX modules have ~1 MHz IF bandwidth.

**Modulation limit:** these RX modules decode **ASK / OOK only**. TPMS, Z-Wave, and most weather-station FSK signals are **not detectable** with this hardware — the UHF ISM Branch (`uhf_ism_branch_v1_0.md` — pending) handles FSK at 868/915 MHz, and FSK at 315/433 must be detected via RTL-SDR on the Trunk in a future enhancement.

---

## 3. Framing

Identical to `wifi24_leaf_protocol_v1_1.md` §2.

---

## 4. Upstream Messages (Leaf → RP2040)

### 4.1 `$SG` — Sub-GHz ASK/OOK Detection

Emitted per recognized pulse train (i.e., per decoded code or per generic raw-bit pattern that passes the noise filter).

```
$SG,leaf_id,freq_khz,mod,protocol_id,raw_code_hex,bit_count,repeat_count,rssi,device_model_id,extra_hex*XX\n
```

| Field | Type | Description |
|---|---|---|
| leaf_id | string | `VHF-315-A`, etc. |
| freq_khz | int | RX frequency in kHz |
| mod | int | 0 = ASK, 1 = OOK (most modules are interchangeable; default 1) |
| protocol_id | int | See §4.1.1 |
| raw_code_hex | hex | Decoded bits as hex (most-significant first); up to 32 hex chars (128 bits) |
| bit_count | int | Number of bits in raw_code_hex (because hex pads to nibble boundaries) |
| repeat_count | int | Number of times the same code was observed in the burst (rolling-code indicator if = 1 with high entropy) |
| rssi | int | dBm if the RX module provides analog RSSI (RXB6 does, RXB12 does not — `-1` if unavailable) |
| device_model_id | int | Specific device classification from the decoder (see §4.1.2); `0` if unknown |
| extra_hex | hex | Decoder-specific extra fields (e.g., temperature + humidity for weather stations); up to 32 hex chars |

**Field count:** 11.

#### 4.1.1 `protocol_id` enum

| Value | Meaning | Decoder |
|---|---|---|
| 0 | Unknown / generic ASK/OOK | Generic pulse-train logger |
| 1 | PT2262 / PT2264 / HT12E (fixed-code generic) | rc-switch |
| 2 | Chamberlain garage (Security+) | Custom |
| 3 | Liftmaster/Linear MCT-11/MCT-3 | Custom |
| 4 | Acurite 5n1 / Atlas weather station | Custom (oversampled bit detection) |
| 5 | LaCrosse weather station (TX29/TX35) | Custom |
| 6 | Oregon Scientific weather (V2 / V3 protocols) | Custom |
| 7 | KAKU / Intertechno (rc-switch supported) | rc-switch |
| 8 | EnerLogic outlets (rc-switch supported) | rc-switch |
| 9 | Generic 24-bit fixed-code | rc-switch |
| 10 | Generic 32-bit fixed-code | rc-switch |
| 11 | Rolling code suspected (high entropy + no repeats) | Pattern heuristic |
| 12–255 | Reserved | |

#### 4.1.2 `device_model_id` enum

For decoded protocols, the Leaf may further identify a device model (e.g., `Acurite 5n1` is a single `protocol_id=4` but has multiple model SKUs distinguishable by message length / ID fields). For v1.0 the field defaults to `0` (unspecified) — analyzer-side enrichment carries the rest.

### 4.2 `$BK` — Batch End (Optional)

When a Leaf has been accumulating multiple repeat copies of the same code, it can wait up to 500 ms after the last repeat before emitting a single `$SG` with `repeat_count=N` and a `$BK` summarizing the window. For most decoders the `$BK` is unnecessary — the `$SG` carries the full picture.

```
$BK,leaf_id,count,window_ms*XX\n
```

Same shape as WiFi24 v1.1 §3.2.

### 4.3 `$HB`

Same shape as WiFi24 v1.1 §3.6. `scan_count` = unique codes detected since boot. `err_count` = decoder mismatch errors + UART RX bad checksums.

---

## 5. Downstream Messages (RP2040 → Leaf)

### 5.1 `$CF` — Configuration

```
$CF,leaf_id,profile_id,noise_floor,debounce_ms,reserved*XX\n
```

| Field | Description |
|---|---|
| profile_id | 1 = decoder set A (common protocols), 2 = decoder set B (diversity twin), 3 = roaming (generic logger), per-Leaf default per §2 |
| noise_floor | Minimum pulse-train length in bits to consider valid (default 8) — filters spurious RF noise |
| debounce_ms | Minimum gap between distinct detections of the same code (default 100) |
| reserved | Send 0 |

**Field count:** 6.

### 5.2 `$CH` — Not Used

Frequency is set by RX-module crystal. The Leaf ignores any `$CH`.

### 5.3 `$PG`, `$RB`

Same as WiFi24 v1.1 §4.3, §4.4.

---

## 6. Boot Sequence

Identical to WiFi24 v1.1 §5. Default `$CF`s per §2.

Standalone fallback (10 s no `$CF`): profile_id=3 (roaming generic logger), under `leaf_id=VHF-???`.

---

## 7. Firmware Architecture — Leaf

### 7.1 Stack: Arduino + rc-switch

Arduino IDE / PlatformIO with the `arduino` framework on AVR. Libraries:
- `rc-switch` for the common ASK protocols (PT2262, KAKU, etc.).
- Custom decoders for Chamberlain, Acurite, LaCrosse, Oregon Scientific bolted on top.

### 7.2 Pin Map (Arduino Nano)

| Function | Pin | Notes |
|---|---|---|
| RX module data in | D2 (INT0) | Edge-triggered interrupt on each rising/falling edge |
| RX module analog RSSI | A0 | If module provides; else floats — read but mark `-1` |
| UART TX → BC | D1 (TX) | Hardware UART |
| UART RX ← BC | D0 (RX) | Hardware UART |
| Status LED | D13 (onboard) | Blink on decoded code |

### 7.3 State Machine

```
[BOOT] → [WAIT_CONFIG] → [CONFIGURE] → [LISTENING]
                                            │
                          (INT0 fires on RX edge)
                          (record edge timestamps in ring)
                                            │
                                  [DECODER_RUN]
                                            │
                          (run all decoders in profile;
                           emit $SG on hit)
                                            │
                                       [IDLE_CHECK]
                                            │
                                   ($HB if due)
                                            ↓
                                       [LISTENING]
```

### 7.4 Pulse-Train Capture

Edge ISR records `micros()` into a 256-entry ring of `uint32_t`s. Main loop checks for gaps > 5 ms (post-burst gap) and runs decoders on the buffered edge list.

```c
volatile uint32_t edges[256];
volatile uint16_t edge_w = 0;
volatile uint8_t  edge_overflow = 0;

ISR(INT0_vect) {
    edges[edge_w++] = micros();
    if (edge_w >= 256) {
        edge_overflow = 1;
        edge_w = 255;
    }
}
```

### 7.5 Decoder Dispatch

```cpp
void loop() {
    if (post_gap_detected()) {
        for (auto &decoder : active_profile) {
            DecodeResult r = decoder.try_decode(edges, edge_count);
            if (r.matched) {
                emit_SG(decoder.protocol_id, r);
                break;   // first match wins
            }
        }
        if (no_decoder_matched && profile_id == 3) {
            emit_SG(0, raw_dump(edges, edge_count));
        }
        clear_edges();
    }
    check_commands();
    if (hb_due()) send_HB();
}
```

### 7.6 Repeat Counting

Most fixed-code remotes (garage doors, etc.) repeat the same code 3–10 times per button press. The decoder collapses consecutive identical codes into a single `$SG` with `repeat_count = N`. A new code (or a 500 ms quiet gap) starts a new accumulator.

### 7.7 UART RX

Hardware UART at 230 400 baud. ATmega328P @ 16 MHz with `U2X=1` gives exact baud (UBRR=8, error 0%). Process incoming bytes byte-by-byte in `loop()` between decoder runs — Leaves don't need to be hyper-responsive to BC commands because the only meaningful commands are `$PG`, `$RB`, occasional `$CF` reconfig.

---

## 8. RP2040 Branch Controller — Architecture

Same dual-core pattern with PIO usage per §1.

### 8.1 Branch ID

`VHF` in all upstream messages.

### 8.2 Dedup Table

| Table | Source | Window | Capacity |
|---|---|---|---|
| `dedup_sg` | `$SG` from all 6 Leaves | 5 s | 256 entries |

Key: `(protocol_id, raw_code_hex)`. Same-code from multiple Leaves (e.g., VHF-433-A and VHF-433-B catching the same weather-station transmission) merges into a single upstream record with the best RSSI.

Tombstone reclaim per BC v1.1 §6.3.

### 8.3 Cross-Frequency Coordination

The BC keeps a `freq_density` histogram (decoded codes per minute per frequency) for the `$BS` heartbeat. Useful for noise-environment monitoring.

---

## 9. Upstream Messages (RP2040 → STM32)

### 9.1 `$SA` — Aggregated Sub-GHz Detection

```
$SA,VHF,timestamp,freq_khz,mod,protocol_id,raw_code_hex,bit_count,repeat_count,rssi,device_model_id,extra_hex,leaf_id,seen_count,time_flag*XX\n
```

| Field | Description |
|---|---|
| seen_count | Distinct receptions of this `(protocol_id, raw_code_hex)` during the 5 s window (across all Leaves) |
| Other fields | Identical to `$SG` semantics |

**Field count:** 15.

### 9.2 `$BS` — Branch Status

```
$BS,VHF,uptime_s,time_valid,fix_ok,pps_age_ms,v315a_st,v315b_st,v315r_st,v433a_st,v433b_st,v433r_st,q_sg_used,dedup_sg_count,err_count*XX\n
```

**Field count:** 15.

---

## 10. Downstream Messages (STM32 → RP2040)

`$TM`, `$RC`, `$RQ` per BC v1.0 §8. `$RC` `leaf_id` namespace: `VHF-315-A/B/R`, `VHF-433-A/B/R`. Anything else dropped.

---

## 11. Module Decomposition

```
branch_vhf_ism/
├── CMakeLists.txt              pico-sdk + CMake
├── pico_sdk_import.cmake
├── README.md
├── include/branch_defs.h
├── pio/
│   ├── uart_rx.pio
│   └── uart_tx.pio
└── src/
    ├── main.c                  6-SM init (4 in PIO0, 2 in PIO1) + TX SM
    ├── core0_leaf_io.{h,c}     6-SM dispatch
    ├── core1_upstream.{h,c}    main loop, dedup, upstream TX
    ├── pio_uart.{h,c}
    ├── proto.{h,c}
    ├── queues.{h,c}             SPSC
    ├── pps_time.{h,c}
    ├── leaf_cmd.{h,c}           $CF/$PG/$RB (no $CH)
    ├── leaf_health.{h,c}
    ├── dedup_sg.{h,c}            5 s window, 256 entries, tombstone
    └── upstream_fmt.{h,c}       $SA / $BS

leaf_vhf_ism/
├── platformio.ini              Arduino AVR, board=nanoatmega328new
├── include/leaf_defs.h
├── lib/
│   ├── rc-switch/              Vendored
│   ├── acurite_decoder/        Custom
│   ├── lacrosse_decoder/       Custom
│   ├── oregon_decoder/         Custom
│   └── chamberlain_decoder/    Custom
└── src/
    ├── main.cpp
    ├── uart_proto.{h,cpp}
    ├── config.{h,cpp}
    ├── heartbeat.{h,cpp}
    ├── cmd_handler.{h,cpp}
    ├── pulse_capture.{h,cpp}   INT0 ring buffer
    └── decoder_dispatch.{h,cpp} profile-driven decoder runner
```

Single binary; `profile_id` from `$CF`.

---

## 12. State Machine — Branch Controller

Same as `branch_controller_wifi24_v1_0.md` §10.

---

## 13. Memory Budget

| Allocation | Size | Notes |
|---|---|---|
| PIO UART RX buffers (6×) | 3 KB | 512 B each |
| Line assembly buffers (6×) | 1.2 KB | |
| `$SG` Queue | 8 KB | 128 × 64 B |
| `dedup_sg` | 32 KB | 256 × 128 B (raw_code_hex + extra_hex) |
| Stacks | 8 KB | |
| UART0 buffers | 1 KB | |
| **Total** | **~54 KB** | Of 264 KB RP2040 SRAM |

Headroom: ~210 KB.

---

## 14. Build Configuration

### 14.1 RP2040 BC

CMake per `branch_wifi24/` pattern, executable `branch_vhf_ism`. The 6-SM PIO init in `main.c` extends the 5-SM Meshtastic pattern.

### 14.2 Arduino Nano Leaf

```ini
[env:leaf_vhf_ism]
platform = atmelavr
board = nanoatmega328new
framework = arduino
monitor_speed = 230400
upload_speed = 115200
lib_deps =
    sui77/rc-switch @ ^2.6.4
build_flags =
    -DLEAF_VERSION="1.0.0"
    -DLEAF_BAND_VHF_ISM=1
    -DMAX_LINE_LEN=200
    -DHB_INTERVAL_MS=10000
    -DCONFIG_TIMEOUT_MS=10000
    -DPULSE_RING_SIZE=256
    -DBAUD_U2X=1
```

The `nanoatmega328new` board variant has the modern bootloader (115 200 upload), which is the predominant 2026-era Nano clone. The `BAUD_U2X` flag enables the 2× oversampling path needed for accurate 230 400 baud at 16 MHz.

---

## 15. Testing Procedure

### 15.1 Single Leaf

1. Flash VHF-433-A; observe `$HB`.
2. Press the button on a known KAKU/Intertechno remote in range; verify `$SG` with `protocol_id=7`, sane `raw_code_hex`, `repeat_count` 3–10.
3. Listen for 5 minutes near a weather station (Acurite); verify `$SG` with `protocol_id=4` and `extra_hex` carrying the temperature/humidity payload.

### 15.2 Diversity Validation

1. Run VHF-433-A and VHF-433-B simultaneously with antennas spaced ~30 cm.
2. Press a remote at the edge of reliable range; verify at least one Leaf captures every press.
3. Verify the BC's `seen_count > 1` for those captures (both Leaves saw it).

### 15.3 Roaming Profile (R-Leaves)

1. Trigger an unknown 433 MHz device (e.g., a cheap PIR sensor of unidentified make).
2. Verify VHF-433-R emits `protocol_id=0` (generic) with the raw bit stream — analyzer-side identification is possible from raw bits.

### 15.4 315 MHz

1. With a garage-door remote in range, verify VHF-315-A captures the press with `protocol_id=2` (Chamberlain) or `protocol_id=11` (rolling-code suspected) depending on the remote vintage.

### 15.5 BC + 6 Leaves + STM32 #2

End-to-end via STM32 #2 USART2.

---

## 16. Open Items

| # | Item | Status |
|---|---|---|
| 1 | RSSI calibration: RXB6 analog AGC output vs dBm — needs a known-power test signal | Hardware bring-up |
| 2 | TPMS detection: FSK at 315/433 MHz — out of scope (needs FSK RX or RTL-SDR) | Documented limitation |
| 3 | Decoder coverage gaps: Honeywell home-security sensors (5800 series @ 345 MHz) — outside Leaf coverage; flag in analyzer | Not in scope |
| 4 | Rolling-code entropy thresholds for `protocol_id=11` | Bench-tune |
| 5 | Antenna isolation between 315 and 433 Leaves | Hardware |
| 6 | Arduino Nano power budget at sustained ASK RX (~30 mA) × 6 = 180 mA on a USB rail | Acceptable |
| 7 | UART noise from RF environment — verify checksum error rate | Bench, may need shielded UART wiring |
| 8 | Decoder upgrade pipeline: how does a new Acurite product SKU get a decoder added? | Firmware OTA not in scope; bench reflash + redeploy |

---

## 17. Cross-References

- `system_plan_v2.md` §3.4 — Branch VHF ISM detail.
- `system_plan_v2_1_amendment.md` §3.2 — STM32 #2 Branch allocation (USART2).
- `stm32_h753_firmware_v1_0.md` §1.3 — STM32 #2 pin map.
- `meshtastic_branch_v1_0.md` §1 — 5-Leaf BC PIO pattern; this Branch extends to 6 Leaves.
- `wifi24_leaf_protocol_v1_2_amendment.md` §6.4 (passive listen philosophy).
- `branch_controller_wifi24_v1_1_amendment.md` §6.3 (tombstone dedup).
- HANDOFF.md §3 — `subghz_detections` table fields target.
