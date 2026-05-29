# 802.15.4 (Thread / Zigbee / Matter) Branch — Leaf Protocol & BC Firmware Guide

**Version:** 1.0.0
**Date:** 2026-05-29
**Scope:** Leaf protocol + RP2040 Branch Controller firmware for the 802.15.4 Branch
**Target Hardware:** 2–4× ESP32-H2 Leaves + RP2040 Branch Controller
**Dependencies:** Patterns from `wifi24_leaf_protocol_v1_1.md` + v1.2, `branch_controller_wifi24_v1_0.md` + v1.1. Shared infrastructure referenced by section; only protocol-specific deltas are stated here.

---

## 1. Branch Topology

```
RP2040 Branch Controller
├── PIO0 SM0 (GP0 TX, GP1 RX) ── Leaf DOT-1 (channels 11–14)
├── PIO0 SM1 (GP2 TX, GP3 RX) ── Leaf DOT-2 (channels 15–18)
├── PIO0 SM2 (GP4 TX, GP5 RX) ── Leaf DOT-3 (channels 19–22)        [optional]
├── PIO0 SM3 (GP6 TX, GP7 RX) ── Leaf DOT-4 (channels 23–26)        [optional]
├── UART0 (GP12 TX, GP13 RX) ── STM32 #1 UART5 (upstream)
└── 1PPS (GP10 EXTI) ─────────── from STM32 #1 GPS
```

| Leaf count | Channel coverage |
|---|---|
| 2 Leaves (minimum) | DOT-1: ch 11–18, DOT-2: ch 19–26 |
| 3 Leaves | DOT-1: ch 11–14, DOT-2: ch 15–20, DOT-3: ch 21–26 |
| 4 Leaves (full) | DOT-1: ch 11–14, DOT-2: ch 15–18, DOT-3: ch 19–22, DOT-4: ch 23–26 |

The firmware accepts any of the three populations; the BC's `$BS` reports which Leaves are online. A single binary runs on all Leaves; channel set adopted from first `$CF`.

**Important:** the ESP32-H2 has **no WiFi radio** — it is 802.15.4 + BLE 5 only. It is unsuitable for any WiFi Branch (HANDOFF.md §6 design call). For BLE 5 scanning on this Branch, see Open Item #5 below.

---

## 2. 802.15.4 Channel Map

| Channel | Frequency (MHz) | Common use |
|---|---|---|
| 11 | 2405 | Thread (Cisco default), Zigbee Home Automation |
| 12 | 2410 | |
| 13 | 2415 | |
| 14 | 2420 | |
| 15 | 2425 | Zigbee HA default in some regions |
| 16 | 2430 | |
| 17 | 2435 | |
| 18 | 2440 | |
| 19 | 2445 | |
| 20 | 2450 | Zigbee Light Link default |
| 21 | 2455 | |
| 22 | 2460 | Thread |
| 23 | 2465 | |
| 24 | 2470 | |
| 25 | 2475 | Thread, Zigbee (less common, away from WiFi 11) |
| 26 | 2480 | |

These overlap heavily with WiFi 2.4 channels — channel 11 overlaps WiFi ch 1; channel 26 overlaps WiFi ch 11. Antenna placement should provide isolation from the 2.4 GHz WiFi Branch where possible.

### 2.1 Per-Leaf Hopping

Each scan Leaf hops through its assigned set with a default 250 ms dwell per channel. Full sweep at 4 channels per Leaf = 1 s. Adequate to catch periodic beacons (Zigbee BCN typical interval ~250 ms – 4 s) and MLE link advertisements (Thread MLE: ~30 s interval but with high redundancy).

---

## 3. Framing

Identical to `wifi24_leaf_protocol_v1_1.md` §2. No 802.15.4–specific framing changes.

---

## 4. Upstream Messages (Leaf → RP2040)

### 4.1 `$ZD` — 802.15.4 Device Frame

Emitted for every uniquely-identified frame source within a per-Leaf 1 s dedup window. "Unique" = `(pan_id, src_addr, channel)` triple.

```
$ZD,leaf_id,channel,pan_id,addr_mode,src_addr,dst_pan,dst_addr,frame_type,sec,seq_no,rssi,lqi,proto_hint*XX\n
```

| Field | Type | Description |
|---|---|---|
| leaf_id | string | `DOT-1`..`DOT-4` |
| channel | int | 11–26 |
| pan_id | hex | 4 hex chars (16-bit) |
| addr_mode | int | 0 = none, 2 = short (16-bit), 3 = ext (64-bit) |
| src_addr | hex | 4 hex chars (short) or 16 hex chars (ext) |
| dst_pan | hex | Destination PAN ID, 4 hex chars; `FFFF` for broadcast |
| dst_addr | hex | 4 or 16 hex chars depending on dst address mode; `FFFF` for broadcast |
| frame_type | int | 0=Beacon, 1=Data, 2=Ack, 3=MAC Command, 4=Multipurpose, 5=Frag, 6=Ext (per 802.15.4-2020) |
| sec | int | 1 = MAC-layer security bit set, 0 = unencrypted at link layer |
| seq_no | int | Frame sequence number (0–255) |
| rssi | int | dBm |
| lqi | int | Link Quality Indicator 0–255 |
| proto_hint | int | Heuristic protocol classification, see §4.1.1 |

**Field count:** 14.

#### 4.1.1 `proto_hint` heuristic

The Leaf does fast pattern matching after the MAC header to guess the upper-layer protocol:

| Value | Meaning | Evidence |
|---|---|---|
| 0 | Unknown | None of the below match |
| 1 | Thread MLE (Mesh Link Establishment) | UDP port 19788 in 6LoWPAN payload; specific MLE TLV prefix |
| 2 | Thread Network Data | 6LoWPAN dispatch + Thread network data header |
| 3 | Zigbee NWK | Zigbee NWK header (frame control byte pattern) immediately after MAC header |
| 4 | Zigbee ZDO | Zigbee NWK + APS to endpoint 0 |
| 5 | Matter over Thread | Thread MLE + Matter-flagged TLVs; v1.0 is conservative — may report as Thread MLE only |
| 6 | Generic 6LoWPAN | 6LoWPAN dispatch byte without Thread markers |
| 7 | Plain 802.15.4 | No upper-layer recognized |

This is a hint, not authoritative — the analyzer can re-classify based on richer rules.

### 4.2 `$ZB` — 802.15.4 Beacon

Emitted on every distinct beacon frame within the dedup window. Beacons are the most useful records for mapping network topology (they carry PAN coordinator info and superframe specs).

```
$ZB,leaf_id,channel,pan_id,coord_addr_mode,coord_addr,sf_spec,gts_spec,pending_addrs,beacon_payload_hex,rssi,lqi*XX\n
```

| Field | Type | Description |
|---|---|---|
| coord_addr_mode | int | 2 = short, 3 = ext |
| coord_addr | hex | Coordinator address |
| sf_spec | hex | Superframe specification, 4 hex chars (16-bit) |
| gts_spec | hex | GTS specification byte |
| pending_addrs | int | Count of pending addresses in beacon |
| beacon_payload_hex | hex | Beacon Payload (post-superframe-spec), max 48 hex chars (24 bytes) |
| rssi, lqi | int | Standard |

**Field count:** 12.

### 4.3 `$BK`, `$HB`

Same shape as WiFi24 v1.1 §3.2, §3.6. `scan_count` = completed channel-set sweeps.

---

## 5. Downstream Messages (RP2040 → Leaf)

### 5.1 `$CF` — Configuration

```
$CF,leaf_id,channel_set_id,dwell_ms,promisc,reserved*XX\n
```

| Field | Description |
|---|---|
| channel_set_id | 1 = ch 11–14, 2 = ch 15–18, 3 = ch 19–22, 4 = ch 23–26, 5 = ch 11–18, 6 = ch 19–26, 7 = all 11–26 |
| dwell_ms | Per-channel dwell time. Default 250. |
| promisc | 1 = full promiscuous (capture all frames, no PAN filter), 0 = filter to broadcast + own (rarely useful here, default 1) |
| reserved | Send 0 |

**Field count:** 6.

### 5.2 `$CH` — Channel Mask Override (16-bit)

Channels 11–26 fit in a 16-bit mask (bit 0 = channel 11, bit 15 = channel 26).

```
$CH,leaf_id,mask*XX\n
```

**Field count:** 3.

| Decimal | Binary | Channels |
|---|---|---|
| 65535 | `1111111111111111` | All 11–26 |
| 1 | `0000000000000001` | 11 only |
| 0x8004 | `1000000000000100` | 11 + 22 + 25 (common Thread set) |

### 5.3 `$PG`, `$RB`

Same as WiFi24 v1.1 §4.3, §4.4.

---

## 6. Boot Sequence

Identical pattern to WiFi24 v1.1 §5. Default `$CF`s:

- `$CF,DOT-1,1,250,1,0*XX`
- `$CF,DOT-2,2,250,1,0*XX`
- `$CF,DOT-3,3,250,1,0*XX`
- `$CF,DOT-4,4,250,1,0*XX`

Standalone fallback: channel-set 7 (all 11–26), dwell 250 ms, promisc on, `leaf_id=DOT-?`.

---

## 7. Firmware Architecture — 802.15.4 Leaf

### 7.1 Stack: ESP-IDF `ieee802154` Component

```
CONFIG_IEEE802154_ENABLED=y
CONFIG_IEEE802154_RX_BUFFER_SIZE=20
CONFIG_IEEE802154_CCA_MODE_CARRIER_SENSE=y
CONFIG_BT_ENABLED=n        # disable BLE on Leaves; the BLE Branch handles BLE
```

The ESP32-H2 has hardware 802.15.4 baseband. The driver presents an RX callback `esp_ieee802154_receive_done(uint8_t *frame, esp_ieee802154_frame_info_t *info)` that fires per frame.

### 7.2 State Machine

```
[BOOT] → [WAIT_CONFIG] → [CONFIGURE] → [SCANNING_HOP] ──────┐
                                            │                │
                                  (RX callback fires)        │
                                  (parse → ring buffer)      │
                                            │                │
                                       [IDLE_CHECK]          │
                                            │                │
                                  (dwell elapsed: hop,       │
                                   emit $ZD/$ZB drained,     │
                                   $HB if due)               │
                                            │                │
                                            └────────────────┘
```

### 7.3 Promiscuous Setup

```c
esp_ieee802154_enable();
esp_ieee802154_set_promiscuous(true);
esp_ieee802154_set_rx_when_idle(true);
esp_ieee802154_set_channel(channel);
esp_ieee802154_receive();
```

The driver delivers frame buffers + an `info` struct containing RSSI and LQI. No payload reconstruction needed — the H2 provides raw frames.

### 7.4 Per-Frame Parsing

In the callback (run from the IEEE 802.15.4 task — same "do not call UART or printf" rule as the WIDS callbacks):

1. Parse the **Frame Control Field** (first 2 bytes, little-endian):
   - Frame Type (bits 0–2)
   - Security Enabled (bit 3)
   - Frame Pending (bit 4)
   - AR (bit 5) — Ack Request
   - PAN ID Compression (bit 6)
   - Destination Addressing Mode (bits 10–11)
   - Frame Version (bits 12–13)
   - Source Addressing Mode (bits 14–15)
2. Parse Sequence Number (1 byte).
3. Parse addressing fields per the modes (PAN IDs + addresses).
4. Note Auxiliary Security Header presence if Security Enabled bit is set.
5. Optionally inspect post-MAC payload bytes for upper-layer pattern matching (§4.1.1).
6. Push a `DotEvent` to the ring buffer.

### 7.5 `DotEvent` Ring Buffer

```c
struct DotEvent {
    uint64_t local_timer_us;
    uint16_t pan_id;
    uint8_t  addr_mode;          // 0/2/3
    uint8_t  src_addr_buf[8];    // big-endian; 2 or 8 bytes used
    uint16_t dst_pan;
    uint8_t  dst_addr_mode;
    uint8_t  dst_addr_buf[8];
    uint8_t  frame_type;
    uint8_t  sec;
    uint8_t  seq_no;
    int8_t   rssi;
    uint8_t  lqi;
    uint8_t  channel;
    uint8_t  is_beacon;
    uint8_t  payload[48];        // first 48 bytes after MAC header for beacon payload + proto-hint inspection
    uint8_t  payload_len;
    uint8_t  proto_hint;
};
```

Capacity: 64 slots × ~96 bytes ≈ 6 KB. Drop-newest on overflow.

### 7.6 Intra-Leaf Dedup

Open-addressing hash set keyed on `(pan_id << 80) | src_addr_bytes` — 128 entries, reset on `$BK` boundary (per scan-cycle end). Duplicate frames (same source within the dwell) get coalesced: best RSSI wins, last seq_no kept.

---

## 8. RP2040 Branch Controller — Architecture

Same dual-core pattern as `branch_controller_wifi24_v1_0.md` §2. PIO0 = 4 RX SMs (or 2/3 if Leaves are absent), PIO1 = 1 TX with OUT-pin remap.

### 8.1 Branch ID

`DOT` in all upstream messages.

### 8.2 Dedup Tables

| Table | Source | Window | Capacity |
|---|---|---|---|
| `dedup_dev` | `$ZD` from all DOT-* Leaves | 5 s | 512 entries |
| `dedup_bcn` | `$ZB` (beacons) | 30 s | 64 entries |

Key for `dedup_dev`: `(pan_id, src_addr)`. Both 2-byte and 8-byte addresses are accommodated by zero-padding shorts to 8 bytes when hashing. Best-RSSI within window wins.

Key for `dedup_bcn`: `(pan_id, coord_addr)`. Beacon Payload is captured from the first beacon and rolled over to the next window only if changed.

Tombstone reclaim per BC v1.1 §6.3.

### 8.3 No WIDS Path

802.15.4 has security extensions but no native "deauth flood" or "evil twin" analog at the MAC layer. The BC therefore omits WIDS analysis. Future v1.1 may add Thread-specific anomaly detection (e.g., excessive MLE Parent Request rate) once analyzer support is ready.

---

## 9. Upstream Messages (RP2040 → STM32)

### 9.1 `$ZA` — Aggregated 802.15.4 Device

```
$ZA,DOT,timestamp,channel,pan_id,addr_mode,src_addr,frame_type,sec,rssi,lqi,proto_hint,leaf_id,seen_count,time_flag*XX\n
```

| Field | Description |
|---|---|
| seen_count | Frames from this source seen during the 5 s window |
| Other fields | Identical to `$ZD` semantics |

**Field count:** 15.

### 9.2 `$ZN` — Aggregated 802.15.4 Beacon (Network)

```
$ZN,DOT,timestamp,channel,pan_id,coord_addr_mode,coord_addr,sf_spec,gts_spec,pending_addrs,beacon_payload_hex,rssi,lqi,leaf_id,time_flag*XX\n
```

**Field count:** 15.

### 9.3 `$BS` — Branch Status

```
$BS,DOT,uptime_s,time_valid,fix_ok,pps_age_ms,dot1_st,dot2_st,dot3_st,dot4_st,q_dev_used,q_bcn_used,dedup_dev_count,dedup_bcn_count,err_count*XX\n
```

**Field count:** 15.

---

## 10. Downstream Messages (STM32 → RP2040)

`$TM`, `$RC`, `$RQ` per BC v1.0 §8. `$RC` `leaf_id` namespace: `DOT-1` .. `DOT-4`. Anything else dropped.

---

## 11. Module Decomposition

```
branch_dot154/
├── CMakeLists.txt              pico-sdk + CMake
├── pico_sdk_import.cmake
├── README.md
├── include/branch_defs.h
├── pio/
│   ├── uart_rx.pio
│   └── uart_tx.pio
└── src/
    ├── main.c
    ├── core0_leaf_io.{h,c}     up to 4 SMs, dispatch
    ├── core1_upstream.{h,c}    main loop, dual dedup, upstream TX
    ├── pio_uart.{h,c}
    ├── proto.{h,c}
    ├── queues.{h,c}             SPSC
    ├── pps_time.{h,c}
    ├── leaf_cmd.{h,c}           $CF/$CH/$PG/$RB
    ├── leaf_health.{h,c}
    ├── dedup_dev.{h,c}          5 s window, 512 entries
    ├── dedup_bcn.{h,c}          30 s window, 64 entries
    └── upstream_fmt.{h,c}       $ZA / $ZN / $BS

leaf_dot154/
├── platformio.ini              ESP-IDF for ESP32-H2
├── sdkconfig.defaults          IEEE 802.15.4 enabled, BLE disabled
├── include/leaf_defs.h
└── src/
    ├── main.c                  setup/loop, state machine
    ├── uart_proto.{h,cpp}
    ├── config.{h,cpp}
    ├── heartbeat.{h,cpp}
    ├── cmd_handler.{h,cpp}
    ├── dot154_scan.{h,cpp}     ieee802154 driver wrapper
    ├── frame_parser.{h,cpp}    MAC header + proto-hint
    ├── dedup.{h,cpp}            per-Leaf hash set
    └── ring_buf.{h,cpp}         SPSC for callback → main
```

Single binary; identity from `$CF`.

---

## 12. State Machine — Branch Controller

Same as `branch_controller_wifi24_v1_0.md` §10. `INIT_LEAVES` sends `$CF` to all 4 Leaves; Leaves that fail to come up are marked offline.

---

## 13. Memory Budget

| Allocation | Size | Notes |
|---|---|---|
| PIO UART RX buffers (up to 4×) | 2 KB | 512 B each |
| Line assembly buffers (4×) | 800 B | |
| Detection Queue | 12 KB | 256 × 48 B |
| Beacon Queue | 4 KB | 64 × 64 B |
| `dedup_dev` | 32 KB | 512 × 64 B (addr + payload + tombstone) |
| `dedup_bcn` | 4 KB | 64 × 64 B |
| Stacks | 8 KB | |
| UART0 buffers | 1 KB | |
| **Total** | **~64 KB** | Of 264 KB RP2040 SRAM |

Headroom: ~200 KB.

---

## 14. Build Configuration

### 14.1 RP2040 BC

CMake as per `branch_wifi24/` pattern, executable `branch_dot154`.

### 14.2 ESP32-H2 Leaf

```ini
[env:leaf_dot154]
platform = espressif32
board = esp32-h2-devkitc-1
framework = espidf
monitor_speed = 230400
build_flags =
    -DLEAF_VERSION="1.0.0"
    -DLEAF_BAND_DOT154=1
    -DMAX_LINE_LEN=200
    -DHB_INTERVAL_MS=10000
    -DCONFIG_TIMEOUT_MS=10000
    -DDOT_RING_SIZE=64
    -DDOT_SEEN_MAX=128
```

`sdkconfig.defaults` enables the ieee802154 component and disables BLE/WiFi.

---

## 15. Testing Procedure

### 15.1 Single Leaf

1. Flash; observe `$HB`.
2. Park near a known Thread or Zigbee network; verify `$ZD` and `$ZB` arrive within seconds.
3. Use a known Thread Border Router (HomePod Mini, Apple TV, Nest Hub, Eero) — expect MLE on ch 25 with periodic Parent Requests.
4. Verify `proto_hint` correctly tags Thread MLE (1) and Zigbee NWK (3) frames against a known device.

### 15.2 BC + Leaves

1. Wire 2 or 4 Leaves; verify `$BS` shows online counts.
2. Verify `$ZA` rate at the STM32 is 2–10/s in a typical residential setting with 1–3 Zigbee networks.
3. Verify `$ZN` carries non-empty `beacon_payload_hex` for at least one PAN.

### 15.3 BC + STM32 Integration

End-to-end per BC v1.0 §13.4 with the `DOT` Branch active.

---

## 16. Open Items

| # | Item | Status |
|---|---|---|
| 1 | Matter-over-Thread identification — payload pattern reliable enough to set `proto_hint=5`? | Defer to analyzer-side enrichment; v1.0 conservative |
| 2 | Auxiliary Security Header parsing (key index, frame counter) — useful for replay detection? | Out of scope v1.0; flagged via `sec=1` only |
| 3 | Antenna isolation from the 2.4 GHz WiFi Branch | Hardware bring-up |
| 4 | LQI calibration: different H2 SKUs report LQI on different scales — normalize at the BC? | Defer; document in analyzer |
| 5 | Should the H2 also run a BLE 5 scanner concurrently with 802.15.4? | Out of scope; the BLE/BT Branch covers BLE on dedicated S3 Leaves |
| 6 | Final GPIO pin assignments | Preliminary |
| 7 | Power budget: 4× H2 + RP2040 | Not yet analyzed |
| 8 | Thread Mesh Local prefix detection (MLE TLV walk) for "this is a Thread network" confirmation | Defer to v1.1 |

---

## 17. Cross-References

- `system_plan_v2.md` §3.1 + v2.1 amendment §3.1 — Branch slot allocation (STM32 #1 UART5).
- `stm32_h753_firmware_v1_0.md` §1.2 — upstream side pin map and `$RC` namespace.
- `wifi24_leaf_protocol_v1_2_amendment.md` §6.4 (passive listen philosophy), §7.4 (drop-newest).
- `branch_controller_wifi24_v1_1_amendment.md` §6.3 (tombstone dedup), §4.5 (SPSC pattern), §12.1 (PIO0/PIO1).
- HANDOFF.md §6 — "ESP32-H2 has no WiFi radio" design call.
