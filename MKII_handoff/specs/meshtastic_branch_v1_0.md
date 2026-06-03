# Meshtastic / Meshcore Branch — Leaf Protocol & BC Firmware Guide

**Version:** 1.0.0
**Date:** 2026-05-29
**Scope:** Leaf protocol + RP2040 Branch Controller firmware for the Meshtastic / Meshcore detection Branch
**Target Hardware:** 5× Heltec LoRa 32 V3 Leaves (ESP32-S3 + SX1262) + RP2040 Branch Controller
**Dependencies:** Patterns from `wifi24_leaf_protocol_v1_1.md` + v1.2, `branch_controller_wifi24_v1_0.md` + v1.1. Shared infrastructure referenced by section; only Branch-specific deltas are stated here.

---

## 1. Branch Topology

```
RP2040 Branch Controller
├── PIO0 SM0 (GP0 TX, GP1 RX) ── Leaf MESH-1  (915 MHz, US LongFast)
├── PIO0 SM1 (GP2 TX, GP3 RX) ── Leaf MESH-2  (915 MHz, US alternate)
├── PIO0 SM2 (GP4 TX, GP5 RX) ── Leaf MESH-3  (868 MHz, EU default)
├── PIO0 SM3 (GP6 TX, GP7 RX) ── Leaf MESH-4  (433 MHz)
├── PIO1 SM1 (GP8 TX, GP9 RX) ── Leaf MCORE-1 (915 MHz, Meshcore)
├── PIO1 SM0                    TX (OUT-pin remap to GP0/GP2/GP4/GP6/GP8)
├── UART0 (GP12 TX, GP13 RX) ── STM32 #2 USART1 (upstream)
└── 1PPS (GP10 EXTI) ─────────── from STM32 #2 GPS
```

**PIO usage note (extends `branch_controller_wifi24_v1_1_amendment.md` §12.1):** five Leaves exceed PIO0's 4 state-machine cap. PIO1 hosts both the dedicated TX SM (SM0, with OUT-pin remap across all 5 Leaves' TX lines) and one additional RX SM (SM1) for the 5th Leaf. PIO1's 32-word instruction memory fits both programs (RX ~8 instructions + TX ~6 instructions = 14, comfortable). This is the **canonical 5-Leaf BC pattern** — copy it for any future Branch that goes past 4 Leaves.

---

## 2. Per-Leaf Configuration

| Leaf | Module | Frequency | LoRa Preset (SF / BW / CR) | Notes |
|---|---|---|---|---|
| MESH-1 | Heltec LoRa 32 V3 | 906.875 MHz | LongFast (SF11 / 250 kHz / 4/5) | US Meshtastic default (channel index 20) |
| MESH-2 | Heltec LoRa 32 V3 | 905.000 MHz | MediumFast (SF10 / 250 kHz / 4/5) | Common US alternate preset / freq |
| MESH-3 | Heltec LoRa 32 V3 | 869.525 MHz | LongFast (SF11 / 250 kHz / 4/5) | EU 868 MHz Meshtastic default |
| MESH-4 | Heltec LoRa 32 V3 | 433.175 MHz | LongFast (SF11 / 125 kHz / 4/5) | US 433 MHz Meshtastic |
| MCORE-1 | Heltec LoRa 32 V3 | 915.000 MHz | Meshcore default (SF11 / 125 kHz / 4/8) | Meshcore packet format — different from Meshtastic |

These are v1.0 defaults; the `$CF` mechanism allows runtime reconfiguration to any preset.

### 2.1 Listen-Only LoRa Reception

The SX1262 is configured for continuous RX on its assigned frequency and modulation parameters. No transmissions. The DIO1 interrupt fires on `RxDone`, delivering the full packet to the ESP32-S3 over SPI. Packet length is variable but bounded by the LoRa MTU at the configured preset (~250 bytes for LongFast at SF11/250).

The radio receives both Meshtastic and other LoRa traffic on the same frequency + preset. Differentiation between Meshtastic, Meshcore, ELRS, LoRaWAN, and generic LoRa is done by **header pattern matching** on the first few payload bytes (see §4.1.1).

---

## 3. Framing

Identical to `wifi24_leaf_protocol_v1_1.md` §2 on the Leaf↔BC and BC↔STM32 UART links.

---

## 4. Upstream Messages (Leaf → RP2040)

### 4.1 `$LR` — LoRa Packet Detection

Emitted per packet received (no intra-Leaf dedup within sub-second; packets are sparse).

```
$LR,leaf_id,freq_khz,sf,bw_khz,cr_id,rssi,snr_x10,pkt_len,header_hex,proto_hint,node_id_hex,hop_count,chan_hash_hex*XX\n
```

| Field | Type | Description |
|---|---|---|
| leaf_id | string | `MESH-1`..`MESH-4`, `MCORE-1` |
| freq_khz | int | Frequency in kHz (e.g., 906875 for 906.875 MHz) |
| sf | int | Spreading factor 5–12 |
| bw_khz | int | Bandwidth in kHz: 125, 250, 500 |
| cr_id | int | Coding rate: 1 = 4/5, 2 = 4/6, 3 = 4/7, 4 = 4/8 |
| rssi | int | dBm (negative) |
| snr_x10 | int | SNR in dB × 10 (signed) — keeps fractional precision in an integer |
| pkt_len | int | Bytes received |
| header_hex | hex | First 16 bytes of the LoRa payload as hex (used by analyzer for re-classification) |
| proto_hint | int | Heuristic protocol class, see §4.1.1 |
| node_id_hex | hex | Decoded source node ID if recoverable from the protocol header; `0` if unknown |
| hop_count | int | Decoded hop count (Meshtastic / Meshcore route-info field); `-1` if unknown |
| chan_hash_hex | hex | Meshtastic 8-bit channel hash byte if visible at the head of the encrypted payload; `00` otherwise |

**Field count:** 14.

Example (a Meshtastic LongFast US default packet):
```
$LR,MESH-1,906875,11,250,1,-92,80,86,3D2A1C880A1F4F800000000000000001,1,1F4F8800,3,2A*5C
```

#### 4.1.1 `proto_hint` heuristic

The Leaf inspects the first 16 bytes of the payload to guess the protocol:

| Value | Meaning | Evidence |
|---|---|---|
| 0 | Unknown / generic LoRa | None of the below match |
| 1 | Meshtastic (encrypted payload) | First byte == channel hash + valid 32-bit destination + 32-bit source; flag bits in expected positions; 8-byte header structure |
| 2 | Meshcore | First 4 bytes match Meshcore's protocol identifier + version field per its public spec |
| 3 | LoRaWAN uplink (PHY type from MHDR byte 0) | MHDR major == 00; MType in {0x20, 0x40, 0x80, 0xA0, 0xC0} |
| 4 | LoRaWAN downlink | MType in {0x60, 0xA0} |
| 5 | ELRS (telemetry / RC) | SF/BW combo distinctive (SF6/BW500 or SF9/BW500) and packet length matches ELRS frame size |
| 6 | Crossfire (TBS) | Packet length + SF pattern distinct from ELRS |

Note: Meshtastic payloads are encrypted (AES-CTR) under a network-specific key, but the **header** (8 bytes containing destination, source, packet ID, hop limit, flags) is always **plaintext**. The Leaf reads the plaintext header bytes; the encrypted body is logged as raw bytes for the analyzer only.

### 4.2 `$HB`

Same shape as WiFi24 v1.1 §3.6. `scan_count` = packets received since boot.

There is no `$BK` for this Branch — emissions are per-packet, not per-batch.

---

## 5. Downstream Messages (RP2040 → Leaf)

### 5.1 `$CF` — Configuration

```
$CF,leaf_id,freq_khz,sf,bw_khz,cr_id,sync_word,reserved*XX\n
```

| Field | Description |
|---|---|
| freq_khz | RX frequency in kHz |
| sf | Spreading factor 5–12 |
| bw_khz | 125 / 250 / 500 |
| cr_id | 1 = 4/5, 2 = 4/6, 3 = 4/7, 4 = 4/8 |
| sync_word | LoRa sync word, 4 hex chars (e.g., `2B` for Meshtastic public, `34` for LoRaWAN private) |
| reserved | Send 0 |

**Field count:** 8.

### 5.2 `$CH` — Not Used

The Leaf parks on a single freq + preset; no channel mask makes sense. Future v1.1 may define `$CH` as a preset rotation list.

### 5.3 `$PG`, `$RB`

Same as WiFi24 v1.1 §4.3, §4.4.

---

## 6. Boot Sequence

Identical pattern to WiFi24 v1.1 §5. Default `$CF`s as listed in §2.

Standalone fallback (10 s timeout): MESH-1 default (US LongFast).

---

## 7. Firmware Architecture — Leaf

### 7.1 Stack: Arduino on Heltec LoRa 32 V3

The Heltec LoRa 32 V3 is supported under PlatformIO's `heltec_wifi_lora_32_v3` board target. The Arduino framework + the Heltec/RadioLib library provides the SX1262 driver. We use **RadioLib** (small footprint, well-maintained).

### 7.2 State Machine

```
[BOOT] → [WAIT_CONFIG] → [CONFIGURE] → [RX_CONTINUOUS]
                                            │
                              (DIO1 interrupt = RxDone)
                                            │
                                  (read packet via SPI)
                                  (parse → emit $LR)
                                            ↓
                                       [IDLE_CHECK]
                                            │
                                  ($HB if due; cmd handling)
                                            │
                                  re-arm RX → [RX_CONTINUOUS]
```

### 7.3 RadioLib SX1262 Setup

```cpp
SX1262 radio = new Module(SX1262_NSS, SX1262_DIO1, SX1262_RST, SX1262_BUSY);
radio.begin(
    freq_mhz,        // frequency
    bw_khz,          // bandwidth
    sf,              // spreading factor
    cr_denom,        // coding rate denominator (5/6/7/8)
    sync_word,       // sync word
    output_power=0,  // not used (listen-only)
    preamble=8       // standard preamble length
);
radio.setDio1Action(rx_done_isr);
radio.startReceive();
```

The `rx_done_isr` is the data-ready ISR. Same constraint as the WIDS callback: no Serial, no SPI in the ISR — just set a flag. Main loop reads the packet via `radio.readData()`.

### 7.4 Per-Packet Parsing

In the main loop:

```cpp
uint8_t buf[256];
size_t len = radio.getPacketLength();
radio.readData(buf, len);
int rssi = (int)radio.getRSSI();
float snr = radio.getSNR();
// parse first 16 bytes for proto_hint
proto_hint = classify_lora_payload(buf, len);
// extract node ID / hop / chan_hash for Meshtastic if proto_hint == 1
emit_LR(...);
radio.startReceive();   // re-arm
```

Listen window is continuous — `startReceive` is non-timed. The chip wakes the ESP32-S3 only on packet completion.

### 7.5 No Intra-Leaf Dedup

LoRa packets are sparse enough that intra-Leaf dedup is unnecessary at v1.0. The BC's dedup window (§8.2) handles duplicate suppression across Leaves.

---

## 8. RP2040 Branch Controller — Architecture

Same dual-core pattern as `branch_controller_wifi24_v1_0.md` §2 with PIO usage extended per §1 above (PIO1 SM1 added for the 5th Leaf RX).

### 8.1 Branch ID

`MTC` in all upstream messages.

### 8.2 Dedup Table

| Table | Source | Window | Capacity |
|---|---|---|---|
| `dedup_lora` | `$LR` from all 5 Leaves | 60 s | 256 entries |

Key: `(proto_hint, node_id_hex)`. Same-`node_id` packets within the window are merged — best RSSI, highest `hop_count`, max `pkt_len`, latest `header_hex`. Unknown nodes (`node_id_hex == 0`) skip the table and are forwarded individually.

### 8.3 Frequency-Density Classification

Optional Phase-2 enhancement: track packets per minute per `(freq_khz, sf, bw_khz)` triple to identify hotspot frequencies. Not in v1.0 firmware.

---

## 9. Upstream Messages (RP2040 → STM32)

### 9.1 `$LA` — Aggregated LoRa Detection

```
$LA,MTC,timestamp,freq_khz,sf,bw_khz,cr_id,rssi,snr_x10,pkt_len,header_hex,proto_hint,node_id_hex,hop_count,chan_hash_hex,leaf_id,seen_count,time_flag*XX\n
```

| Field | Description |
|---|---|
| seen_count | Packets from this `(proto, node_id)` during the 60 s window |
| Other fields | Identical to `$LR` semantics |

**Field count:** 18.

### 9.2 `$BS` — Branch Status

```
$BS,MTC,uptime_s,time_valid,fix_ok,pps_age_ms,mesh1_st,mesh2_st,mesh3_st,mesh4_st,mcore1_st,q_lora_used,dedup_lora_count,err_count*XX\n
```

**Field count:** 14.

---

## 10. Downstream Messages (STM32 → RP2040)

`$TM`, `$RC`, `$RQ` per BC v1.0 §8. `$RC` `leaf_id` namespace: `MESH-1` .. `MESH-4`, `MCORE-1`. Anything else dropped.

---

## 11. Module Decomposition

```
branch_meshtastic/
├── CMakeLists.txt              pico-sdk + CMake
├── pico_sdk_import.cmake
├── README.md
├── include/branch_defs.h
├── pio/
│   ├── uart_rx.pio
│   └── uart_tx.pio
└── src/
    ├── main.c                  init incl. 5th RX SM in PIO1
    ├── core0_leaf_io.{h,c}     5-SM dispatch
    ├── core1_upstream.{h,c}    main loop, dedup, upstream TX
    ├── pio_uart.{h,c}
    ├── proto.{h,c}
    ├── queues.{h,c}             SPSC
    ├── pps_time.{h,c}
    ├── leaf_cmd.{h,c}           $CF/$PG/$RB (no $CH)
    ├── leaf_health.{h,c}
    ├── dedup_lora.{h,c}         60 s window, 256 entries, tombstone
    └── upstream_fmt.{h,c}       $LA / $BS

leaf_meshtastic/
├── platformio.ini              Arduino, Heltec LoRa 32 V3
├── include/leaf_defs.h
└── src/
    ├── main.cpp
    ├── uart_proto.{h,cpp}
    ├── config.{h,cpp}
    ├── heartbeat.{h,cpp}
    ├── cmd_handler.{h,cpp}
    ├── lora_rx.{h,cpp}          RadioLib SX1262 wrapper
    └── proto_classify.{h,cpp}   payload pattern matching
```

Single binary for all 5 Leaves; identity from `$CF`.

---

## 12. State Machine — Branch Controller

Same as `branch_controller_wifi24_v1_0.md` §10. `INIT_LEAVES` sends `$CF` to all 5 Leaves with the v1.0 defaults from §2.

---

## 13. Memory Budget

| Allocation | Size | Notes |
|---|---|---|
| PIO UART RX buffers (5×) | 2.5 KB | 512 B each |
| Line assembly buffers (5×) | 1 KB | |
| LoRa Packet Queue | 16 KB | 64 × ~256 B (incl. header_hex + payload margin) |
| `dedup_lora` | 32 KB | 256 × 128 B (entries are larger than WiFi BSSID — full header_hex stored) |
| Stacks | 8 KB | |
| UART0 buffers | 1 KB | |
| **Total** | **~60 KB** | Of 264 KB RP2040 SRAM |

Headroom: ~204 KB.

---

## 14. Build Configuration

### 14.1 RP2040 BC

CMake per `branch_wifi24/` pattern, executable `branch_meshtastic`. Additional PIO setup in `main.c` for SM1 on PIO1 (RX) per §1.

### 14.2 Heltec LoRa 32 V3 Leaf

```ini
[env:leaf_meshtastic]
platform = espressif32
board = heltec_wifi_lora_32_v3
framework = arduino
monitor_speed = 230400
lib_deps =
    jgromes/RadioLib @ ^6.6.0
build_flags =
    -DLEAF_VERSION="1.0.0"
    -DLEAF_BAND_LORA=1
    -DMAX_LINE_LEN=200
    -DHB_INTERVAL_MS=10000
    -DCONFIG_TIMEOUT_MS=10000
    -DLORA_DEFAULT_FREQ_KHZ=906875
    -DLORA_DEFAULT_SF=11
    -DLORA_DEFAULT_BW=250
    -DLORA_DEFAULT_CR_ID=1
    -DLORA_DEFAULT_SYNC=0x2B
```

The OLED on the Heltec V3 (SSD1306) is not used by firmware; it may optionally display the Leaf ID and packet count for bring-up.

---

## 15. Testing Procedure

### 15.1 Single Leaf

1. Flash MESH-1 firmware; observe `$HB`.
2. Bring a powered Meshtastic node (T-Beam, RAK4630, etc.) into range running US LongFast.
3. Trigger a node-info broadcast on the test node (e.g., reboot it).
4. Verify `$LR` appears with `proto_hint=1`, recognizable `node_id_hex`, `rssi` near expected for the test distance.

### 15.2 Multi-Preset

1. Configure a second Meshtastic node to MediumFast preset; verify MESH-2 captures it (and MESH-1 doesn't because preset differs).
2. Repeat for EU 868 against MESH-3 (band-edge test).

### 15.3 Meshcore Differentiation

1. Bring a Meshcore-running test node; verify MCORE-1 captures it with `proto_hint=2`.
2. Verify MESH-1 (Meshtastic SF11/250) does **not** capture Meshcore (SF11/125 + different sync word).

### 15.4 BC + 5 Leaves

1. Wire all 5 Leaves; verify `$BS` shows 5 online.
2. Run for 30 minutes near an active Meshtastic mesh; verify `$LA` rate is 5–20/min depending on mesh activity.

### 15.5 BC + STM32 #2

End-to-end via the STM32 #2 USART1 path.

---

## 16. Open Items

| # | Item | Status |
|---|---|---|
| 1 | Meshtastic preset rotation: scan multiple presets per Leaf instead of parking | Defer to v1.1; SX1262 retune is ~10 ms so a slow rotation is feasible |
| 2 | Decoded Meshtastic channel hash → Meshtastic channel name mapping in firmware vs analyzer | Analyzer side (it has the channel-name DB) |
| 3 | ELRS / Crossfire detection on 915 MHz Leaves (`proto_hint=5/6`) — verify on hardware | Pending field data |
| 4 | LoRaWAN gateway detection from observed traffic patterns | Future enhancement |
| 5 | Meshcore protocol fields stabilization — Meshcore is younger and the wire format may shift | Track upstream, refresh `proto_classify.cpp` accordingly |
| 6 | Final GPIO pin assignments incl. PIO1 SM1 RX pin | Preliminary |
| 7 | Antenna selection per band (915 vs 868 vs 433) — separate antennas needed per Leaf | Hardware bring-up |
| 8 | Power budget (5 Heltec V3 + RP2040, peak RX ~30 mA each) | Approx 200 mA peak, comfortable on a USB rail |

---

## 17. Cross-References

- `system_plan_v2.md` §3.3 — Branch Meshtastic / Meshcore detail.
- `system_plan_v2_1_amendment.md` §3.2 — STM32 #2 Branch allocation (USART1).
- `stm32_h753_firmware_v1_0.md` §1.3 — STM32 #2 pin map and `$RC` namespace.
- `wifi24_leaf_protocol_v1_2_amendment.md` §7.4 (drop-newest), §6.4 (passive listen philosophy).
- `branch_controller_wifi24_v1_1_amendment.md` §6.3 (tombstone dedup), §4.5 (SPSC pattern), §12.1 (PIO0/PIO1 — extended here for 5 Leaves).
- HANDOFF.md §3 — `lora_detections` table fields target.
