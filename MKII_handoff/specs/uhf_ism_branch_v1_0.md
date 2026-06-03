# UHF ISM 868 / 915 MHz Branch — Leaf Protocol & BC Firmware Guide

**Version:** 1.0.0
**Date:** 2026-05-29
**Scope:** Leaf protocol + RP2040 Branch Controller firmware for the UHF ISM Branch
**Target Hardware:** 8× heterogeneous Leaves (mix of HT-HC33 HaLow + Arduino Nano w/ SX1262 LoRa + Arduino Nano w/ CC1101/RFM69 FSK) + RP2040 Branch Controller
**Dependencies:** Patterns from prior Branch specs. Heavily references `meshtastic_branch_v1_0.md` (SX1262 LoRa flow) and `vhf_ism_branch_v1_0.md` (Arduino Nano pulse capture + BC dedup pattern).

This is the most heterogeneous Branch — 8 Leaves spread across three radio technologies (WiFi HaLow, LoRa, FSK). It is also the first Branch to exceed PIO state-machine capacity, requiring use of an RP2040 hardware UART (UART1) as an 8th-Leaf link.

---

## 1. Branch Topology

```
RP2040 Branch Controller
├── PIO0 SM0 (GP0 TX, GP1 RX)   ── Leaf UHF-HALOW       (HT-HC33, 902–928 MHz HaLow)
├── PIO0 SM1 (GP2 TX, GP3 RX)   ── Leaf UHF-915-IOT-A   (Nano + SX1262, 915 MHz LoRaWAN sweeping)
├── PIO0 SM2 (GP4 TX, GP5 RX)   ── Leaf UHF-915-IOT-B   (Nano + SX1262, 915 MHz alt)
├── PIO0 SM3 (GP6 TX, GP7 RX)   ── Leaf UHF-915-ISM-A   (Nano + CC1101, 915 MHz FSK)
├── PIO1 SM1 (GP8 TX, GP9 RX)   ── Leaf UHF-915-ISM-B   (Nano + CC1101, 915 MHz FSK alt)
├── PIO1 SM2 (GP14 TX, GP15 RX) ── Leaf UHF-915-UAS     (Nano + SX1262, 915 MHz ELRS/Crossfire patterns)
├── PIO1 SM3 (GP18 TX, GP19 RX) ── Leaf UHF-868-A       (Nano + SX1262, 868 MHz)
├── PIO1 SM0                    ── TX (OUT-pin remap across PIO-connected Leaves only)
├── UART1 (GP20 TX, GP21 RX)    ── Leaf UHF-868-B       (Nano + CC1101, 868 MHz FSK) — RP2040 hardware UART
├── UART0 (GP12 TX, GP13 RX)    ── STM32 #2 UART4 (upstream)
└── 1PPS (GP10 EXTI)            ── from STM32 #2 GPS
```

**PIO + UART1 hybrid (new pattern for 8-Leaf Branches):** PIO0 + PIO1 together host at most 7 RX SMs alongside the shared TX SM (4 RX in PIO0, 1 TX + 3 RX in PIO1). The 8th Leaf is wired to the RP2040's hardware UART1 (PL011), which has its own dedicated pins, FIFOs, and DMA channels. UART1 is otherwise unused on the BC (UART0 is the upstream STM32 link).

This frees a PIO TX-pin-remap target for the 8th Leaf (downstream commands to UHF-868-B go through UART1's TX). UART1 is the canonical "overflow Leaf" slot for any future 8-Leaf Branch.

---

## 2. Per-Leaf Configuration

| Leaf | Radio module | Frequency | Modulation / mode | Target signals |
|---|---|---|---|---|
| UHF-HALOW | HT-HC33 (Heltec / Morse Micro MM6108-based) | 902–928 MHz (US S1G) | 802.11ah HaLow | WiFi HaLow APs (smart-home gateways, agricultural sensors) |
| UHF-915-IOT-A | Arduino Nano + SX1262 | 902.3 → 914.9 MHz hop | LoRa SF7–SF12 sweep, BW 125 | LoRaWAN uplink (US915 8-channel sub-band) |
| UHF-915-IOT-B | Arduino Nano + SX1262 | 923.3 → 927.5 MHz hop | LoRa SF7–SF12 sweep, BW 125 | LoRaWAN downlink + alt sub-bands |
| UHF-915-ISM-A | Arduino Nano + CC1101 | 908.42 MHz | FSK (Z-Wave R1/R2/R3) | Z-Wave network detection |
| UHF-915-ISM-B | Arduino Nano + CC1101 | 902–928 MHz scan | FSK, GFSK, OOK channel scan | Amazon Sidewalk (FSK 902–928), generic FSK ISM |
| UHF-915-UAS | Arduino Nano + SX1262 | 915 MHz | LoRa SF6/BW500 + SF9/BW500 | ELRS 915 (LoRa C2); Crossfire 915 |
| UHF-868-A | Arduino Nano + SX1262 | 868.1 MHz LoRaWAN-EU + 868.3 + 868.5 | LoRa SF7–SF12 sweep, BW 125 | EU LoRaWAN, ELRS 868, Meshtastic EU |
| UHF-868-B | Arduino Nano + CC1101 | 868 MHz scan | FSK, GFSK | EnOcean, KNX-RF, EU smart-home FSK |

### 2.1 LoRa Preset Sweeping

The LoRaWAN Leaves (`UHF-915-IOT-A/B`, `UHF-868-A`) need to scan multiple SF values to catch the full LoRaWAN data-rate range. SX1262 retune is ~5–10 ms. v1.0 implements a slow sweep: hold each preset for 200 ms, then advance to the next. At SF7–SF12 sweep (6 presets × 200 ms = 1.2 s/cycle), a single Leaf cycles through the full range every 1.2 s.

LoRaWAN packet length per SF (at BW 125, payload 51 bytes typical uplink):
- SF7: ~50 ms airtime — easily caught in a 200 ms window.
- SF12: ~1.6 s airtime — **not** caught in a single 200 ms dwell. SF12 captures rely on the receiver being on the right SF when the packet starts; the SF12 detection rate will be ~12% of actual SF12 traffic. Acceptable for v1.0.

A better SF12 detection would require a dedicated Leaf parked on SF12 continuously. Open Item #5.

### 2.2 UAS Detection Leaf

UHF-915-UAS uses SX1262 LoRa with **SF6 / BW500** primarily (ELRS 500 Hz mode signature) and **SF9 / BW500** as the secondary sweep target (ELRS 50 Hz mode). These SF/BW combos are distinctive — generic LoRaWAN uses BW125 — so detection of these short packets with this SF/BW combination strongly suggests an ELRS link.

Crossfire uses different framing (LoRa with smaller payload, SF8/BW125 sometimes). The Leaf catches some Crossfire indirectly via UHF-915-IOT-A's sweep but doesn't have a dedicated mode in v1.0.

### 2.3 HaLow Caveats

The HT-HC33 (and similar MM6108-based modules) expose 802.11ah via SPI or UART control. It does **not** emit raw frames at the level the ESP32-C3 does; instead it provides a higher-level API (scan results, association events). For listen-only the Leaf uses the module's scan mode and reports detected APs. The data is sparse — HaLow APs are still uncommon in 2026.

---

## 3. Framing

Identical to `wifi24_leaf_protocol_v1_1.md` §2.

---

## 4. Upstream Messages (Leaf → RP2040)

This Branch's heterogeneity means **three** distinct upstream message types:

### 4.1 `$HA` — HaLow AP Detection (from UHF-HALOW)

```
$HA,leaf_id,BSSID,SSID_hex,RSSI,channel,enc,bandwidth_mhz,time_flag*XX\n
```

| Field | Type | Description |
|---|---|---|
| BSSID | MAC | HaLow AP MAC |
| SSID_hex | hex | Hex-encoded SSID, up to 64 hex chars |
| RSSI | int | dBm |
| channel | int | S1G channel number (1–37 per IEEE 802.11ah-2016) |
| enc | int | Same enum as `wifi24_leaf_protocol_v1_2_amendment.md` §3.1.1 (0–10) |
| bandwidth_mhz | int | 1, 2, 4, 8, or 16 (HaLow widths) |
| time_flag | int | 0/1 |

**Field count:** 9.

### 4.2 `$LR` — LoRa Packet Detection

Identical to `meshtastic_branch_v1_0.md` §4.1, with the `proto_hint` enum extended:

| Value | Meaning |
|---|---|
| 3 | LoRaWAN uplink |
| 4 | LoRaWAN downlink |
| 5 | ELRS |
| 6 | Crossfire |
| 7 | EU 868 LoRaWAN (regional flavor distinction) |
| 8 | DragonLink / OpenLRS (deprecated, but catches legacy gear) |
| Otherwise | as Meshtastic spec |

Emitted by all SX1262 Leaves (`UHF-915-IOT-A`, `UHF-915-IOT-B`, `UHF-915-UAS`, `UHF-868-A`).

### 4.3 `$FS` — FSK Packet / Activity Detection

Emitted by CC1101 Leaves (`UHF-915-ISM-A`, `UHF-915-ISM-B`, `UHF-868-B`).

```
$FS,leaf_id,freq_khz,baud_kbps,dev_khz,modulation,rssi,pkt_len,header_hex,proto_hint,sync_word_hex,time_flag*XX\n
```

| Field | Type | Description |
|---|---|---|
| baud_kbps | int | Detected bit rate × 10 (preserves 0.1 kbps precision) |
| dev_khz | int | Frequency deviation in kHz (FSK only; `0` for OOK/GFSK with unknown dev) |
| modulation | int | 0 = FSK, 1 = GFSK, 2 = MSK, 3 = OOK |
| rssi | int | dBm |
| pkt_len | int | Bytes received (or estimated for short captures) |
| header_hex | hex | First 16 bytes of payload (hex) |
| proto_hint | int | See §4.3.1 |
| sync_word_hex | hex | Detected sync-word value, up to 8 hex chars |

**Field count:** 13.

#### 4.3.1 FSK `proto_hint` enum

| Value | Meaning | Detection rule |
|---|---|---|
| 0 | Unknown FSK | None of the below |
| 1 | Z-Wave (908.42 MHz) | Frequency + 9.6 kbps + standard sync word `0x5555AAAA` |
| 2 | Sidewalk (FSK at 902–928) | Frequency + Sidewalk-specific framing pattern |
| 3 | EnOcean (868 MHz) | 125 kbps GFSK + 8-byte EnOcean preamble |
| 4 | KNX-RF (868 MHz) | 16.4 kbps + KNX preamble |
| 5 | Generic ITU-R 868 SRD | 868 MHz band + generic FSK |
| 6 | Generic 915 ISM | Generic FSK in 902–928 |

### 4.4 `$HB`

Same shape as WiFi24 v1.1 §3.6.

---

## 5. Downstream Messages (RP2040 → Leaf)

### 5.1 `$CF` — Configuration (Variable Format Per Radio Type)

The `$CF` payload varies by Leaf radio type. The Leaf is identified by `leaf_id`; the BC knows each Leaf's type from `branch_defs.h` and constructs the right payload.

#### HaLow Leaf (`UHF-HALOW`)
```
$CF,UHF-HALOW,scan_interval_ms,channel_mask_hex,reserved*XX\n
```

#### SX1262 LoRa Leaves
```
$CF,leaf_id,sweep_mode,base_freq_khz,sf_min,sf_max,bw_khz,cr_id,sync_word*XX\n
```

| Field | Description |
|---|---|
| sweep_mode | 0 = fixed (use `sf_min` only), 1 = SF7..SF12 sweep, 2 = LoRaWAN US sub-band hop |

#### CC1101 FSK Leaves
```
$CF,leaf_id,freq_khz,baud_kbps,dev_khz,modulation,sync_word_hex,sync_len_bits*XX\n
```

| Field | Description |
|---|---|
| sync_word_hex | 4 hex chars (16-bit) or 8 hex chars (32-bit) |
| sync_len_bits | 16 or 32 |

All variants stay within 200 bytes line length.

### 5.2 `$CH` — Not Used (Fixed Per Radio Type)

The Leaves' channel/frequency is set via `$CF`. The BC drops any standalone `$CH`.

### 5.3 `$PG`, `$RB`

Same as WiFi24 v1.1 §4.3, §4.4.

---

## 6. Boot Sequence

Identical pattern to WiFi24 v1.1 §5. The BC issues per-Leaf `$CF` payloads with v1.0 defaults from §2.

Standalone fallback (10 s timeout): each Leaf type assumes a sensible default — SX1262 Leaves park on US LoRaWAN ch 0 (903.9 MHz / SF10 / BW125), CC1101 Leaves park on 902.3 MHz / 9.6 kbps FSK, HaLow Leaf parks on channel 1.

---

## 7. Firmware Architecture — Leaves (Per Radio Type)

### 7.1 HaLow Leaf (Arduino + HT-HC33)

The HT-HC33 module is driven over UART or SPI (model-dependent) using its AT-command set. The Arduino Nano polls scan results and translates to `$HA` upstream. Polling interval default 5 s.

The HT-HC33 firmware itself does the heavy lifting (HaLow PHY + association detection). The Leaf is effectively a UART-to-UART adapter that reformats results.

### 7.2 SX1262 LoRa Leaves (Arduino + SX1262)

Reuses the Meshtastic Leaf firmware (`leaf_meshtastic`) with:
- Sweep loop added that retunes SF every 200 ms.
- LoRaWAN-aware header parsing (MHDR + DevAddr extraction).
- ELRS pattern matcher on UHF-915-UAS.

Build flag `LEAF_KIND_LORAWAN_SWEEP=1` vs `LEAF_KIND_UAS=1` to differentiate the binaries (multiple firmware images for this Branch, unlike WiFi24's single-binary model).

### 7.3 CC1101 FSK Leaves (Arduino + CC1101)

The CC1101 is a programmable sub-GHz transceiver. Library: `LSatan/SmartRC-CC1101-Driver-Lib` (Arduino).

```cpp
ELECHOUSE_cc1101.Init();
ELECHOUSE_cc1101.setMHZ(freq_mhz);
ELECHOUSE_cc1101.setModulation(2);    // 2-FSK
ELECHOUSE_cc1101.setDRate(baud_kbps);
ELECHOUSE_cc1101.setDeviation(dev_khz);
ELECHOUSE_cc1101.setSyncWord(sw_hi, sw_lo);
ELECHOUSE_cc1101.setSyncMode(2);      // 16-bit sync
ELECHOUSE_cc1101.SetRx();
```

The CC1101 raises GDO0 on packet received. Arduino reads the buffer over SPI, including a final byte with RSSI.

Sidewalk and Z-Wave have **different** baud/dev/sync configurations — the UHF-915-ISM Leaves rotate through 4–6 known configurations every 250 ms to catch each protocol family. The retune is ~3 ms per change, so the duty cycle of any one preset is ~96%.

---

## 8. RP2040 Branch Controller — Architecture

Same dual-core pattern as `branch_controller_wifi24_v1_0.md` §2 with the PIO + UART1 hybrid from §1.

### 8.1 Branch ID

`UHF` in all upstream messages.

### 8.2 Dedup Tables

Three independent dedup tables, one per upstream message type:

| Table | Source | Window | Capacity | Key |
|---|---|---|---|---|
| `dedup_halow` | `$HA` | 5 s | 64 | BSSID |
| `dedup_lora` | `$LR` | 60 s | 256 | `(proto_hint, node_id_hex)` |
| `dedup_fsk` | `$FS` | 30 s | 128 | `(freq_khz, proto_hint, sync_word_hex)` |

Tombstone reclaim per BC v1.1 §6.3.

### 8.3 No WIDS Path

Same rationale as the BLE/BT Branch — no native flood/twin analog at the FSK or LoRa layer in v1.0.

---

## 9. Upstream Messages (RP2040 → STM32)

### 9.1 `$HW` — Aggregated HaLow AP

```
$HW,UHF,timestamp,BSSID,SSID_hex,RSSI,channel,enc,bandwidth_mhz,leaf_id,time_flag*XX\n
```

**Field count:** 11.

### 9.2 `$LA` — Aggregated LoRa Detection

Identical to `meshtastic_branch_v1_0.md` §9.1 with `branch_id = UHF` and the extended `proto_hint` enum.

**Field count:** 18.

### 9.3 `$FA` — Aggregated FSK Detection

```
$FA,UHF,timestamp,freq_khz,baud_kbps,dev_khz,modulation,rssi,pkt_len,header_hex,proto_hint,sync_word_hex,leaf_id,seen_count,time_flag*XX\n
```

**Field count:** 15.

### 9.4 `$BS` — Branch Status

```
$BS,UHF,uptime_s,time_valid,fix_ok,pps_age_ms,halow_st,iota_st,iotb_st,isma_st,ismb_st,uas_st,e68a_st,e68b_st,q_used_max,dedup_lora_count,dedup_fsk_count,err_count*XX\n
```

**Field count:** 18.

---

## 10. Downstream Messages (STM32 → RP2040)

`$TM`, `$RC`, `$RQ` per BC v1.0 §8. `$RC` `leaf_id` namespace: all 8 names from §1. The UART1-attached Leaf (UHF-868-B) has its `$RC` routed via UART1's TX path rather than the PIO1 SM0 pin-remap.

---

## 11. Module Decomposition

```
branch_uhf_ism/
├── CMakeLists.txt
├── pico_sdk_import.cmake
├── README.md
├── include/branch_defs.h
├── pio/
│   ├── uart_rx.pio
│   └── uart_tx.pio
└── src/
    ├── main.c                  init incl. PIO0+PIO1 SMs + UART1 driver
    ├── core0_leaf_io.{h,c}     7-SM dispatch + UART1 line assembly
    ├── core1_upstream.{h,c}    main loop, triple dedup, upstream TX
    ├── pio_uart.{h,c}
    ├── hw_uart.{h,c}            UART1 RX/TX wrapper for the 8th Leaf
    ├── proto.{h,c}
    ├── queues.{h,c}             SPSC
    ├── pps_time.{h,c}
    ├── leaf_cmd.{h,c}           per-radio-type $CF builders
    ├── leaf_health.{h,c}
    ├── dedup_halow.{h,c}
    ├── dedup_lora.{h,c}
    ├── dedup_fsk.{h,c}
    └── upstream_fmt.{h,c}       $HW / $LA / $FA / $BS

leaf_halow/             Arduino, AT-command driver for HT-HC33
leaf_uhf_lorawan/       Arduino, SX1262 with SF sweep
leaf_uhf_uas/           Arduino, SX1262 with ELRS-style SF/BW combos
leaf_uhf_fsk/           Arduino, CC1101 with rotating presets
```

Four Leaf firmware images. Manufacturing flashes each Nano with its role-specific image.

---

## 12. State Machine — Branch Controller

Same as `branch_controller_wifi24_v1_0.md` §10. `INIT_LEAVES` sends per-Leaf `$CF` payloads per §2.

---

## 13. Memory Budget

| Allocation | Size | Notes |
|---|---|---|
| PIO UART RX buffers (7×) | 3.5 KB | 512 B each |
| Hardware UART1 RX FIFO | 256 B | RP2040 PL011 deepens via DMA channel |
| Line assembly buffers (8×) | 1.6 KB | |
| LoRa Queue | 8 KB | 32 × 256 B |
| FSK Queue | 4 KB | 32 × 128 B |
| HaLow Queue | 1 KB | 16 × 64 B |
| `dedup_lora` | 32 KB | 256 × 128 B |
| `dedup_fsk` | 16 KB | 128 × 128 B |
| `dedup_halow` | 4 KB | 64 × 64 B |
| Stacks | 8 KB | |
| UART0 buffers | 1 KB | |
| **Total** | **~80 KB** | Of 264 KB RP2040 SRAM |

Headroom: ~184 KB.

---

## 14. Build Configuration

### 14.1 RP2040 BC

CMake per the existing pattern, executable `branch_uhf_ism`. Adds `hardware_uart1` link target (already part of `hardware_uart`).

### 14.2 Leaf Build Targets

```ini
[env:leaf_halow]
platform = atmelavr
board = nanoatmega328new
framework = arduino
monitor_speed = 230400
build_flags =
    -DLEAF_VERSION="1.0.0"
    -DLEAF_KIND_HALOW=1

[env:leaf_uhf_lorawan]
platform = atmelavr
board = nanoatmega328new
framework = arduino
monitor_speed = 230400
lib_deps = jgromes/RadioLib @ ^6.6.0
build_flags =
    -DLEAF_VERSION="1.0.0"
    -DLEAF_KIND_LORAWAN_SWEEP=1

[env:leaf_uhf_uas]
platform = atmelavr
board = nanoatmega328new
framework = arduino
monitor_speed = 230400
lib_deps = jgromes/RadioLib @ ^6.6.0
build_flags =
    -DLEAF_VERSION="1.0.0"
    -DLEAF_KIND_UAS=1

[env:leaf_uhf_fsk]
platform = atmelavr
board = nanoatmega328new
framework = arduino
monitor_speed = 230400
lib_deps = LSatan/SmartRC-CC1101-Driver-Lib @ ^2.5.7
build_flags =
    -DLEAF_VERSION="1.0.0"
    -DLEAF_KIND_FSK=1
```

---

## 15. Testing Procedure

### 15.1 HaLow

1. Provision an HT-HC33 in known-AP-present environment (or set up a Heltec MM6108 AP nearby).
2. Verify `$HA` arrives with sane BSSID + channel + bandwidth.

### 15.2 LoRaWAN Sweep

1. Place near an active LoRaWAN gateway (TheThingsNetwork outdoor coverage helps).
2. Verify `$LR` with `proto_hint=3` over a 10-minute window.
3. Confirm sweep is active: `$HB.scan_count` increments at the expected sweep rate.

### 15.3 UAS Detection

1. Place near an active ELRS 915 link (e.g., a quadcopter with TX powered on the bench).
2. Verify `$LR` with `proto_hint=5` (ELRS) on UHF-915-UAS within seconds.

### 15.4 Z-Wave + Sidewalk + EnOcean

1. With known Z-Wave hub broadcasting; verify `$FS` with `proto_hint=1`.
2. Sidewalk: an active Amazon Echo with Sidewalk-enabled neighbor — verify `$FS` with `proto_hint=2` (low rate, may take 30 min).
3. EnOcean (868 MHz, EU): trigger an EnOcean wireless switch nearby; verify `$FS` with `proto_hint=3` on UHF-868-B.

### 15.5 Integration

End-to-end through STM32 #2 UART4.

---

## 16. Open Items

| # | Item | Status |
|---|---|---|
| 1 | HT-HC33 sourcing + driver — module pipeline still maturing in 2026 | Pending sourcing |
| 2 | UART1 line-assembly module shares state shape with PIO Leaves — refactor target | Minor |
| 3 | CC1101 sync-word table for protocols — maintenance burden as new protocols emerge | Living document; refresh annually |
| 4 | LoRaWAN SF12 detection rate (12%) — improve with a dedicated SF12 Leaf? | Future v1.1 — would push Branch to 9 Leaves (UART0 unavailable for a Leaf; would need an I2C-to-UART bridge) |
| 5 | ELRS variant coverage — ELRS 2.4 GHz is on the FPV Branch, not here | Documented split |
| 6 | Power budget: 8 Nanos + 1 HaLow module ≈ 350 mA peak | Budget |
| 7 | Antennas: SMA per Leaf, 868/915 antennas need ground-plane separation | Hardware |
| 8 | Final GPIO pin assignments incl. UART1 pins | Preliminary |

---

## 17. Cross-References

- `system_plan_v2.md` §3.5 — Branch UHF ISM detail.
- `system_plan_v2_1_amendment.md` §3.2 — STM32 #2 Branch allocation (UART4).
- `stm32_h753_firmware_v1_0.md` §1.3 — STM32 #2 pin map.
- `meshtastic_branch_v1_0.md` §4.1 — base `$LR` format and `proto_hint` enum (extended here).
- `vhf_ism_branch_v1_0.md` §4.1 — sub-GHz aggregate dedup pattern.
- `branch_controller_wifi24_v1_1_amendment.md` §12.1 — PIO pattern (extended with UART1 hybrid here).
- HANDOFF.md §3 — `lora_detections` + `subghz_detections` table targets.
