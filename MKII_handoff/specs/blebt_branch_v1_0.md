# BLE / BT Classic Branch — Leaf Protocol & BC Firmware Guide

**Version:** 1.0.0
**Date:** 2026-05-29
**Scope:** Leaf protocol + RP2040 Branch Controller firmware for the BLE / BT Classic Branch
**Target Hardware:** 2× ESP32-S3 Leaves (BLE scanner + BT Classic inquiry scanner) + RP2040 Branch Controller
**Dependencies:** Patterns from `wifi24_leaf_protocol_v1_1.md` + v1.2, `branch_controller_wifi24_v1_0.md` + v1.1. Shared infrastructure (NMEA framing, `$HB`/`$CF`/`$PG`/`$RB`, SPSC pattern, PIO0=RX/PIO1=TX, PPS + `$TM`, tombstone dedup) is referenced by section; only protocol-specific deltas and the new message types are stated here.

---

## 1. Branch Topology

```
RP2040 Branch Controller
├── PIO0 SM0 (GP0 TX, GP1 RX) ── Leaf BLE-1 (BLE scanner, ESP32-S3)
├── PIO0 SM1 (GP2 TX, GP3 RX) ── Leaf BT-1 (BT Classic inquiry, ESP32-S3)
├── PIO0 SM2 ───────────────────  (unused; reserved for future BLE long-range/Coded PHY Leaf)
├── PIO0 SM3 ───────────────────  (unused)
├── UART0 (GP12 TX, GP13 RX) ── STM32 #1 UART4 (upstream)
└── 1PPS (GP10 EXTI) ─────────── from STM32 #1 GPS
```

| Leaf | Role | Stack | Notes |
|---|---|---|---|
| BLE-1 | BLE 5 passive scanner | NimBLE on ESP-IDF | All three primary adv channels (37/38/39); secondary adv via Extended Adv if enabled |
| BT-1 | BT Classic inquiry-scan listener | Bluedroid (BT Classic on ESP32-S3) | Listens for inquiry requests + FHS responses |

**Strict listen-only.** No probes, no scan requests, no inquiry transmissions, no pairing. Per HANDOFF §1 the platform is passive. The BLE Leaf uses `BLE_SCAN_TYPE_PASSIVE` (no scan requests). The BT Classic Leaf operates in **inquiry-scan** mode only — it does not initiate inquiries; it only responds to inquiries from nearby devices (which are themselves discoverable-by-design transmissions). This is a strict subset of what most "BT scanners" do; it deliberately misses devices that are not currently in inquiry-scan mode themselves. The limitation is documented and accepted.

---

## 2. Bands, Channels, PHYs (Context)

### 2.1 BLE Primary Advertising

| Channel | Frequency (MHz) | Notes |
|---|---|---|
| 37 | 2402 | Below WiFi |
| 38 | 2426 | Between WiFi ch 6 and 11 |
| 39 | 2480 | Above WiFi ch 11 |

Beacons (advertising PDUs) hop through 37 → 38 → 39 with a per-event interval of ~20 ms – 10 s depending on device. The ESP32 BLE controller scans all three by default. PHYs:

| PHY | Bit rate | Notes |
|---|---|---|
| LE 1M | 1 Mbps | Default for all legacy advertising |
| LE 2M | 2 Mbps | Data-channel only; not used for adv |
| LE Coded (S=2 or S=8) | 500 / 125 kbps | Long-range (Bluetooth 5.0+); seen in Coded PHY adv events |

The firmware enables `esp_ble_gap_set_ext_scan_params` to capture **both** 1M and Coded primary advertising channels — common for trackers and asset tags.

### 2.2 Extended Advertising (BLE 5.0+)

Extended adv uses secondary channels (37 GHz–wide ones in the data-channel range). The Leaf enables extended scanning so adv data >31 bytes is captured (chained PDUs). Periodic advertising is acknowledged but reported as the first PDU only — full periodic sync is out of scope for v1.0.

### 2.3 BT Classic Inquiry / FHS

BT Classic discovery uses **General Inquiry Access Code (GIAC)** at 79-channel frequency hopping. Devices in inquiry-scan mode listen for GIAC and reply with FHS (Frequency Hop Synchronization) packets containing their BDADDR and clock. The Leaf listens for FHS responses on the inquiry-scan train.

In addition to FHS, the Leaf can decode **EIR (Extended Inquiry Response)** payloads when present — these carry device name, COD, and service UUIDs.

---

## 3. Framing

Identical to `wifi24_leaf_protocol_v1_1.md` §2 (NMEA-style `$..*XX\n`, 230 400 baud, 200-byte max, XOR checksum, hex-encoded text fields). The `bdaddr` field uses the same 17-character colon-delimited format as WiFi BSSID.

---

## 4. Upstream Messages (Leaf → RP2040)

### 4.1 `$BL` — BLE Detection (Primary)

Emitted by Leaf `BLE-1` for each captured advertising PDU after a per-source dedup of 1 s (intra-Leaf — coarser dedup happens on the RP2040).

```
$BL,leaf_id,bdaddr,addr_type,name_hex,rssi,channel,adv_type,flags,conn,company_id,svc_uuid16,phy*XX\n
```

| Field | Type | Description |
|---|---|---|
| leaf_id | string | `BLE-1` |
| bdaddr | MAC | BD address, uppercase colon hex |
| addr_type | int | 0 = public, 1 = random static, 2 = RPA, 3 = NRPA |
| name_hex | hex | Local Name (Complete tag 0x09 or Shortened tag 0x08), max 32 hex chars (16 bytes); `00` if none |
| rssi | int | dBm |
| channel | int | 37, 38, 39, or 0 if secondary-channel extended adv |
| adv_type | int | 0=ADV_IND, 1=ADV_DIRECT_IND, 2=ADV_NONCONN_IND, 3=SCAN_RSP, 4=ADV_SCAN_IND, 5=EXT_ADV |
| flags | int | LE adv flags byte (LE General Discoverable, BR/EDR Not Supported, etc.) |
| conn | int | 1 = connectable, 0 = non-connectable |
| company_id | int | 16-bit Company Identifier from Manufacturer Specific Data (decimal); `-1` if no MSD |
| svc_uuid16 | hex | First 16-bit Complete/Incomplete Service UUID present, 4 hex chars; `0000` if none |
| phy | int | 1 = LE 1M, 2 = LE 2M, 3 = LE Coded S=2, 4 = LE Coded S=8 |

**Field count:** 13.

Example (a Flock Safety ALPR beacon — Company ID `0x09C8`):
```
$BL,BLE-1,52:11:33:9A:11:01,2,00,-58,37,2,06,0,2504,0000,1*4A
```

### 4.2 `$BX` — BLE Extended Payload (Optional Secondary)

When a packet's full Manufacturer Specific Data, full service-UUID list, or extended local name doesn't fit in `$BL` (truncation occurred), the Leaf emits a follow-up `$BX` line with the verbatim payload. Correlation is by `bdaddr` + timestamp window on the BC.

```
$BX,leaf_id,bdaddr,manuf_data_hex,svc_uuid_list_hex,name_full_hex*XX\n
```

| Field | Description |
|---|---|
| manuf_data_hex | Up to 60 hex chars (30 bytes); truncate beyond and set a low bit somewhere (see note) |
| svc_uuid_list_hex | All advertised service UUIDs concatenated (16-bit first, then 128-bit; each space-padded) |
| name_full_hex | Full local name beyond 16 bytes; empty if not over-length |

**Field count:** 6.

Total ~190 bytes worst case; rarely emitted (most beacons fit in `$BL`).

### 4.3 `$BT` — BT Classic Detection

Emitted by Leaf `BT-1` for each FHS / EIR captured.

```
$BT,leaf_id,bdaddr,name_hex,rssi,cod,eir_present,time_flag*XX\n
```

| Field | Description |
|---|---|
| leaf_id | `BT-1` |
| bdaddr | BT Classic BD_ADDR |
| name_hex | EIR Local Name if present; `00` otherwise |
| rssi | dBm (from inquiry-scan callback) |
| cod | 3-byte Class of Device, 6 hex chars (e.g., `240414` for Smart phone) |
| eir_present | 1 if EIR data was present in the response (not just FHS) |
| time_flag | 0 = PPS-synced, 1 = degraded |

**Field count:** 8.

### 4.4 `$BK` — Batch End

Emitted by BLE-1 at the end of each 1 s dedup-flush window:

```
$BK,leaf_id,count,window_ms*XX\n
```

Same shape as WiFi24 v1.1 §3.2. The Leaf's intra-window dedup keeps a tiny hash set of BD addresses; one `$BL` per unique address per window is emitted.

### 4.5 `$HB` — Heartbeat

Same shape as WiFi24 v1.1 §3.6 (`uptime_s, free_heap, scan_count, err_count`). For BLE-1, `scan_count` is the number of completed 1 s scan windows. For BT-1, it's the number of inquiry-scan cycles (each ~10.24 s by Bluetooth spec convention).

---

## 5. Downstream Messages (RP2040 → Leaf)

### 5.1 `$CF` — Configuration

```
$CF,leaf_id,mode,phy_mask,window_ms,interval_ms*XX\n
```

| Field | Description |
|---|---|
| mode | 0 = BLE scan, 1 = BT Classic inquiry-scan |
| phy_mask | BLE only: bit 0 = LE 1M, bit 1 = LE Coded (default `3` = both). BT Classic ignores. |
| window_ms | BLE: scan window in ms (default 1000). BT Classic: inquiry-scan window (default 1280). |
| interval_ms | BLE: scan interval in ms (default 1000 — 100% duty cycle). BT Classic: inquiry-scan interval (default 2560). |

**Field count:** 6.

### 5.2 `$CH` — Not Used

The BLE Leaf scans all three primary adv channels concurrently — no channel-mask control is meaningful. The BC drops any `$CH` it receives for this Branch.

### 5.3 `$PG`, `$RB`

Same as WiFi24 v1.1 §4.3, §4.4.

---

## 6. Boot Sequence

Identical pattern to WiFi24 v1.1 §5. Default `$CF`s:

- `$CF,BLE-1,0,3,1000,1000*XX` — BLE both PHYs, 100% duty cycle.
- `$CF,BT-1,1,0,1280,2560*XX` — BT Classic standard inquiry-scan timing.

Standalone fallback (10 s no `$CF`): BLE-1 enters mode 0 with default params under `leaf_id=BLE-?`; BT-1 enters mode 1.

---

## 7. Firmware Architecture — BLE Leaf (BLE-1)

### 7.1 State Machine

```
[BOOT] → [WAIT_CONFIG] → [CONFIGURE] → [SCANNING] ──────┐
                                            │           │
                                  (scan callback fires) │
                                  (drain to ring buffer)│
                                            │           │
                                       [IDLE_CHECK] ────┘
                                            │
                                   (every 1 s: emit $BL × N, $BK)
```

### 7.2 Stack: NimBLE on ESP-IDF

NimBLE (Apache Mynewt port shipped with ESP-IDF) is smaller and has better Coded PHY support than Bluedroid for BLE-only use. Configuration via `sdkconfig`:

```
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT=y
CONFIG_BT_NIMBLE_EXT_ADV=y
CONFIG_BT_NIMBLE_OBSERVER=y
CONFIG_BT_NIMBLE_BROADCASTER=n
CONFIG_BT_NIMBLE_CENTRAL=n
CONFIG_BT_NIMBLE_PERIPHERAL=n
```

### 7.3 Scan Setup

```c
struct ble_gap_ext_disc_params params_1m = {
    .itvl = 0x0010,   // 10 ms
    .window = 0x0010, // 10 ms — 100% duty cycle
    .passive = 1,
};
struct ble_gap_ext_disc_params params_coded = params_1m;
ble_gap_ext_disc(BLE_OWN_ADDR_PUBLIC, 0, 0, 0, 0,
                 &params_1m, &params_coded, &event_handler);
```

The `event_handler` callback fires per advertising event. **Critical rule** (same as WiFi24 promiscuous): do not call UART or NimBLE host functions from the callback. Write the parsed `BleAdvEvent` into a ring buffer and return. Main loop drains and emits `$BL`/`$BX`.

### 7.4 Per-Event Parsing

In the callback, extract:

- BD address + type.
- RSSI (`event->disc.rssi`).
- Primary PHY (`event->disc.prim_phy`).
- Channel (derived from event metadata if available; else 0 for ext).
- Walk the adv data TLV (length, type, value) to extract:
  - Flags (tag 0x01)
  - 16-bit service UUIDs (tags 0x02, 0x03)
  - 128-bit service UUIDs (tags 0x06, 0x07) — surfaced via `$BX` if present
  - Local Name (tags 0x08, 0x09)
  - TX Power Level (tag 0x0A)
  - Manufacturer Specific Data (tag 0xFF) — Company ID is the first 2 bytes (little-endian)

### 7.5 Intra-Leaf Dedup

A small open-addressing hash set of `(bdaddr, channel)` over the current 1 s window — 64 entries. Reset on `$BK` boundary.

Devices using Resolvable Private Addresses (RPAs) rotate their MAC every ~15 min, so the same physical device may appear under many BD addresses across a drive. **No attempt is made to resolve RPAs in the firmware** — that's an analyzer-side task using companion data (Company ID, name, manufacturer data patterns).

### 7.6 Ring Buffer

```c
struct BleAdvEvent {
    uint64_t local_timer_us;
    uint8_t  bdaddr[6];
    uint8_t  addr_type;
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  adv_type;
    uint8_t  flags;
    uint8_t  conn;
    uint16_t company_id;        // 0xFFFF = none
    uint16_t svc_uuid16;        // 0x0000 = none
    uint8_t  phy;
    uint8_t  name_len;
    uint8_t  name[32];
    uint8_t  manuf_data_len;
    uint8_t  manuf_data[31];
    uint8_t  truncated;         // 1 if anything was too long; emit $BX
};
```

Slots: 32. Per-slot ~80 bytes ≈ 2.5 KB. Drop-newest on overflow (matches Leaf v1.2 §7.4).

---

## 8. Firmware Architecture — BT Classic Leaf (BT-1)

### 8.1 Stack: Bluedroid (BT Classic on ESP32-S3)

ESP-IDF's Bluedroid stack supports BR/EDR on ESP32-S3.

```
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_BT_CLASSIC_ENABLED=y
CONFIG_BT_BLE_ENABLED=n          // BLE handled by separate Leaf
CONFIG_BT_GAP_BL_SCAN_PARAM=y
```

### 8.2 Inquiry-Scan Setup

```c
esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
// Configure inquiry-scan window and interval per $CF:
esp_bt_gap_set_inquiry_scan_params(window_slots, interval_slots);  // 1 slot = 625 µs
esp_bt_gap_register_callback(gap_callback);
```

The `gap_callback` fires on `ESP_BT_GAP_DISC_RES_EVT` when an FHS/EIR response is captured.

### 8.3 Per-Event Parsing

In the callback:
- BD address (BD_ADDR).
- COD from EIR if present; else from FHS (Class of Device offset).
- RSSI from `param->disc_res.prop[i]` where `code == ESP_BT_GAP_DEV_PROP_RSSI`.
- EIR data (full record): iterate the EIR TLV blocks for Local Name (0x08 / 0x09), service UUIDs, etc.

Emit `$BT` per unique BDADDR within a 10 s dedup window.

### 8.4 Listen-Only Caveats

- Without initiating inquiry, BT-1 captures only devices that are themselves in inquiry-scan mode emitting FHS to other devices' inquiries — typically devices in pairing mode.
- Most BT Classic devices in normal operation will **not** be visible. This is an accepted limitation of strict passive operation.
- The BC's `$BS` indicates this expected low detection rate is normal, not a fault.

---

## 9. RP2040 Branch Controller — Architecture

Same dual-core pattern as `branch_controller_wifi24_v1_0.md` §2:

- **Core 0:** PIO UART RX from 2 Leaves, line assembly, dispatch.
- **Core 1:** PPS + `$TM`; dedup (5 s window for BLE; 30 s for BT Classic — much lower detection rate); upstream TX to STM32.

### 9.1 Branch ID

`BLE` in all upstream messages (`branch_id` field).

### 9.2 Dedup Windows

BLE detections are dense (~5–50 unique BDADDRs/s in urban). BT Classic detections are sparse (~1 per minute in suburban). The BC runs two independent dedup tables:

| Table | Source | Window | Capacity |
|---|---|---|---|
| `dedup_ble` | `$BL` from BLE-1 | 5 s | 1024 entries |
| `dedup_bt` | `$BT` from BT-1 | 30 s | 128 entries |

Tombstone reclaim per BC v1.1 §6.3.

Same-BDADDR coalescing keeps the strongest RSSI within the window; final `$WA`-equivalent (`$BD` / `$BC_T`) is emitted at window close.

### 9.3 No Evil-Twin Analog

BLE/BT have no direct evil-twin analog (BLE adv doesn't carry the "associated network" semantics of WiFi). The BC therefore omits the WiFi24-style WIDS-analysis path; the WIDS Queue and WIDS-thread responsibilities collapse into the dedup path.

---

## 10. Upstream Messages (RP2040 → STM32)

### 10.1 `$BD` — Deduplicated BLE Detection

```
$BD,BLE,timestamp,bdaddr,addr_type,rssi,channel,adv_type,company_id,svc_uuid16,name_hex,leaf_id,phy,time_flag*XX\n
```

| Field | Description |
|---|---|
| branch_id | `BLE` |
| timestamp | UTC epoch with 6 decimal places |
| bdaddr | BD address |
| addr_type | 0=public, 1=random static, 2=RPA, 3=NRPA |
| rssi | Best RSSI within window |
| channel | 37/38/39/0 |
| adv_type | Per §4.1 |
| company_id | Decimal; `-1` if none |
| svc_uuid16 | 4 hex chars |
| name_hex | Up to 16 bytes hex; `00` if none |
| leaf_id | `BLE-1` |
| phy | 1/2/3/4 |
| time_flag | 0/1 |

**Field count:** 14.

### 10.2 `$BX` — Extended Payload Pass-Through

Forwarded verbatim from the Leaf when `truncated=1`. Same shape as Leaf §4.2 with `branch_id` and `timestamp` prepended:

```
$BX,BLE,timestamp,bdaddr,manuf_data_hex,svc_uuid_list_hex,name_full_hex,time_flag*XX\n
```

**Field count:** 8.

### 10.3 `$BC_T` — Deduplicated BT Classic Detection

(Named `BC_T` to avoid clash with `$BC` beacon from WIDS in the WiFi24 Branch — even though they live on different STM32 UARTs the Trunk demux is simpler with disjoint message-type prefixes.)

```
$BC_T,BLE,timestamp,bdaddr,name_hex,rssi,cod,eir_present,time_flag*XX\n
```

**Field count:** 9.

### 10.4 `$BS` — Branch Status

```
$BS,BLE,uptime_s,time_valid,fix_ok,pps_age_ms,ble1_st,bt1_st,q_ble_used,q_bt_used,dedup_ble_count,dedup_bt_count,err_count*XX\n
```

**Field count:** 13.

---

## 11. Downstream Messages (STM32 → RP2040)

`$TM`, `$RC`, `$RQ` as per BC v1.0 §8. `$RC` `leaf_id` namespace: `BLE-1`, `BT-1`. Anything else dropped.

---

## 12. Module Decomposition

```
branch_blebt/
├── CMakeLists.txt              pico-sdk + CMake
├── pico_sdk_import.cmake
├── README.md
├── include/branch_defs.h
├── pio/
│   ├── uart_rx.pio
│   └── uart_tx.pio
└── src/
    ├── main.c
    ├── core0_leaf_io.{h,c}     2 SMs, BLE/BT line dispatch
    ├── core1_upstream.{h,c}    main loop, dual dedup, upstream TX
    ├── pio_uart.{h,c}
    ├── proto.{h,c}
    ├── queues.{h,c}             SPSC for BLE events + BT events
    ├── pps_time.{h,c}
    ├── leaf_cmd.{h,c}           $CF/$PG/$RB (no $CH)
    ├── leaf_health.{h,c}
    ├── dedup_ble.{h,c}          5 s window, 1024 entries, tombstone
    ├── dedup_bt.{h,c}           30 s window, 128 entries, tombstone
    └── upstream_fmt.{h,c}       $BD / $BX / $BC_T / $BS

leaf_ble/
├── platformio.ini              ESP-IDF for ESP32-S3
├── sdkconfig.defaults          NimBLE + ext adv + Coded PHY
└── src/...                     NimBLE observer, $BL/$BX emitter

leaf_bt_classic/
├── platformio.ini              ESP-IDF for ESP32-S3
├── sdkconfig.defaults          Bluedroid + BT Classic
└── src/...                     Inquiry-scan listener, $BT emitter
```

Note: BLE and BT Classic Leaves run **different firmware binaries** (NimBLE vs Bluedroid; ESP-IDF doesn't support both stacks simultaneously on S3). They are not the "single binary, identity from $CF" pattern that WiFi24 uses. Manufacturing must flash each S3 with its role-specific image.

---

## 13. State Machine — Branch Controller

Same as `branch_controller_wifi24_v1_0.md` §10. `INIT_LEAVES` sends `$CF` to BLE-1 (mode=0, phy_mask=3, 1000 ms / 1000 ms) and BT-1 (mode=1, 1280 ms / 2560 ms).

---

## 14. Memory Budget

| Allocation | Size | Notes |
|---|---|---|
| PIO UART RX buffers (2×) | 1 KB | 512 B each |
| Line assembly buffers (2×) | 400 B | |
| BLE event queue (Core 0 → Core 1) | 4 KB | 32 × ~128 B |
| BT event queue | 1 KB | 16 × 64 B |
| `dedup_ble` | 64 KB | 1024 × 64 B (BD address + name + cached payload + tombstone byte) |
| `dedup_bt` | 8 KB | 128 × 64 B |
| Time state | 32 B | |
| Stacks | 8 KB | |
| UART0 buffers | 1 KB | |
| **Total** | **~88 KB** | Out of 264 KB RP2040 SRAM |

BLE dedup is the largest allocation by far because BLE detections are dense and BD addresses + names need more storage than a bare WiFi BSSID. Headroom: ~176 KB.

---

## 15. Build Configuration

### 15.1 RP2040 BC (`branch_blebt/`)

`CMakeLists.txt` mirrors `branch_wifi24/`'s. Executable `branch_blebt`.

### 15.2 BLE Leaf (`leaf_ble/`)

```ini
[env:leaf_ble]
platform = espressif32
board = esp32-s3-devkitc-1
framework = espidf
monitor_speed = 230400
build_flags =
    -DLEAF_VERSION="1.0.0"
    -DLEAF_KIND_BLE=1
    -DMAX_LINE_LEN=200
    -DHB_INTERVAL_MS=10000
    -DBLE_DEDUP_WINDOW_MS=1000
    -DBLE_RING_SIZE=32
```

`sdkconfig.defaults` as in §7.2.

### 15.3 BT Classic Leaf (`leaf_bt_classic/`)

```ini
[env:leaf_bt_classic]
platform = espressif32
board = esp32-s3-devkitc-1
framework = espidf
monitor_speed = 230400
build_flags =
    -DLEAF_VERSION="1.0.0"
    -DLEAF_KIND_BT=1
    -DMAX_LINE_LEN=200
    -DHB_INTERVAL_MS=10000
    -DBT_INQUIRY_SCAN_WINDOW_MS=1280
    -DBT_INQUIRY_SCAN_INTERVAL_MS=2560
```

`sdkconfig.defaults` as in §8.1.

---

## 16. Testing Procedure

### 16.1 BLE Leaf

1. Flash, observe `$HB` at boot.
2. Place near an Apple device (continuous Continuity beacons on Company ID `0x004C`); verify `$BL` lines arrive with `company_id=76`.
3. Place near a Flock ALPR test target (Company ID `0x09C8`); verify `$BL` with `company_id=2504`.
4. Verify three primary channels appear in detections over 1 minute (37/38/39 should all be observed).
5. Verify a known Coded-PHY device (BLE 5 long-range tracker) reports `phy=3` or `phy=4`.

### 16.2 BT Classic Leaf

1. Flash, observe `$HB`.
2. Put a smartphone into BT pairing mode (now actively scanning); verify `$BT` arrives within 30 s.
3. Verify `cod` matches phone class (`240414` = Smartphone, `2A0418` = Smart Phone with handsfree).
4. Walk past a discoverable BT speaker; verify capture.

### 16.3 BC + Both Leaves

1. Wire both Leaves; verify `$BS` shows `ble1_st=1, bt1_st=1` within 30 s.
2. Verify dedup: walk through a dense BLE environment for 60 s; `$BD` rate at STM32 should be 5–20/s; `$BX` ~10% of that.
3. Verify timestamps PPS-synced (`time_flag=0`).

### 16.4 BC + STM32

End-to-end: `$BD` → STM32 USART4 → USB CDC stream → host-side capture; verify `bdaddr`, `company_id`, `timestamp` all present and well-formed.

---

## 17. Open Items

| # | Item | Status |
|---|---|---|
| 1 | RPA resolution: defer to analyzer; firmware does not handle | Accepted limitation |
| 2 | Periodic advertising sync (BLE 5.0+) — full sync capture | Out of scope v1.0 |
| 3 | BT Classic active inquiry as an optional non-strict-passive mode (config flag) | Out of scope; conflicts with HANDOFF §1 |
| 4 | Antenna: BLE/BT 2.4 GHz omnidirectional vs WiFi24 patches — physical separation needed | Hardware bring-up |
| 5 | NimBLE Coded PHY scan stability on ESP-IDF v5.x — verify on hardware | Pending |
| 6 | Bluedroid memory footprint at compile time (BT Classic enabled) — confirm under S3 free heap budget | Pending compile |
| 7 | Flock ALPR (Company ID 0x09C8), Raven detector (Service UUID TBD) device signatures — pull from prior Wardriving Analyzer DB | Analyzer-side enrichment, not firmware |
| 8 | Final GPIO pin assignments | Preliminary |
| 9 | Power: 2× ESP32-S3 with both BT and WiFi radios consume more than C3; budget pending | Hardware analysis |

---

## 18. Cross-References

- `system_plan_v2.md` §3.1 + v2.1 amendment §3.1 — Branch slot allocation (STM32 #1 UART4).
- `stm32_h753_firmware_v1_0.md` §1.2 — upstream side pin map and `$RC` namespace.
- `wifi24_leaf_protocol_v1_2_amendment.md` §7.4 (ring drop-newest), §6.4 (passive listen philosophy).
- `branch_controller_wifi24_v1_1_amendment.md` §6.3 (tombstone dedup), §4.5 (SPSC pattern), §12.1 (PIO0/PIO1).
- HANDOFF.md §1 — strict-passive design constraint (governs the BT Classic "no active inquiry" decision).
- HANDOFF.md §3 — surveillance device detection (Flock ALPR Company ID `0x09C8`, Raven detector via Service UUID).
