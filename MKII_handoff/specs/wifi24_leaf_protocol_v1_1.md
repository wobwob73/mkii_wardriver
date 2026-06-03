# 2.4 GHz WiFi Branch — Leaf Protocol Specification & Firmware Guide

**Version:** 1.1.0
**Date:** 2026-04-04
**Scope:** Leaf ↔ RP2040 Branch Controller messaging for the 2.4 GHz WiFi Branch
**Target Hardware:** ESP32-C3 (all Leaves)
**Changes from 1.0.0:** Removed ESP8266 support. Single-platform target. Full WIDS beacon parsing. Increased buffer sizes.

---

## 1. Branch Topology

```
RP2040 Branch Controller
├── PIO0 SM0 (GP0 TX, GP1 RX) ── Leaf W1 (Scan, ch 1)
├── PIO0 SM1 (GP2 TX, GP3 RX) ── Leaf W2 (Scan, ch 6)
├── PIO0 SM2 (GP4 TX, GP5 RX) ── Leaf W3 (Scan, ch 11)
├── PIO0 SM3 (GP6 TX, GP7 RX) ── Leaf W4 (WIDS, hop all)
├── UART0 or UART1 ──────────── STM32 (upstream)
└── I2C (GP8 SDA, GP9 SCL) ──── BN-220 GPS (Branch-local)
```

All Leaf connections are point-to-point, full duplex. No shared bus, no buffer ICs.

**Baud rate:** 230400, 8N1
**Pin assignments:** Preliminary — subject to final PCB layout.

---

## 2. Framing

NMEA-style ASCII framing on all messages in both directions.

```
$TYPE,FIELD1,FIELD2,...,FIELDn*XX\n
```

| Element | Description |
|---------|-------------|
| `$` | Start-of-frame (0x24) |
| `TYPE` | 2-character message type identifier |
| `,` | Field delimiter |
| `*` | Checksum delimiter (0x2A) |
| `XX` | XOR checksum: all bytes between `$` and `*` (exclusive), uppercase hex |
| `\n` | End-of-frame (0x0A) |

### 2.1 Constraints

- Maximum line length: 200 bytes (receiver discards lines exceeding this)
- No `$`, `*`, or `\n` permitted in field values
- Empty fields are zero-length between delimiters: `,,`
- All integer fields are decimal unless otherwise noted
- MAC addresses are colon-delimited uppercase hex: `AA:BB:CC:DD:EE:FF`

### 2.2 SSID Encoding

SSIDs can contain commas, asterisks, NULs, and arbitrary bytes. All SSIDs are transmitted as hex-encoded byte sequences.

| SSID (UTF-8) | Hex Encoding |
|---|---|
| `MyWiFi` | `4D7957694669` |
| (empty/broadcast) | `00` |
| (hidden, zero-length) | `00` |
| `Café` (with UTF-8 é) | `436166C3A9` |

Maximum: 32 bytes raw = 64 hex characters.

The Branch Controller is responsible for decoding hex back to UTF-8 for upstream reporting.

### 2.3 Checksum Calculation

```
XOR of every byte between '$' and '*', exclusive of both.

Example: $AP,W1,AA:BB:CC:DD:EE:FF,4D7957694669,-72,1,4,0*XX
         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
         XOR these bytes

Result printed as two uppercase hex digits.
```

Pseudocode:
```
checksum = 0
for each byte between '$' and '*':
    checksum ^= byte
hex_str = uppercase_hex(checksum, 2 digits)
```

### 2.4 Receiver Behavior

On receiving a line:
1. Find `$` — discard all bytes before it
2. Find `*` — if not found before 200 bytes, discard line
3. Find `\n` — if not found within 4 bytes after `*`, discard line
4. Compute XOR checksum of bytes between `$` and `*`
5. Compare against received `XX` — if mismatch, discard line and increment error counter
6. Parse fields by splitting on `,`
7. Validate field count for the given message type — if wrong, discard

No retransmission. No ACK. Corrupted lines are silently dropped. The heartbeat mechanism provides visibility into error rates.

---

## 3. Upstream Messages (Leaf → RP2040)

### 3.1 `$AP` — WiFi AP Detection

Emitted by scan Leaves (W1–W3) for each AP found in a scan cycle.

```
$AP,leaf_id,BSSID,SSID_hex,RSSI,channel,enc,hidden*XX\n
```

| Field | Type | Range/Format | Description |
|---|---|---|---|
| leaf_id | string | `W1`–`W4` | Leaf identifier |
| BSSID | MAC | `AA:BB:CC:DD:EE:FF` | AP MAC address |
| SSID_hex | hex string | 2–64 chars | Hex-encoded SSID (see §2.2) |
| RSSI | int | `-120` to `0` | Signal strength in dBm |
| channel | int | `1`–`14` | AP's reported operating channel |
| enc | int | `0`–`8` | Encryption type (see §3.1.1) |
| hidden | int | `0` or `1` | 1 if SSID was empty/hidden in beacon |

**Field count:** 8 (including message type prefix)

Example:
```
$AP,W1,A0:B1:C2:D3:E4:F5,4D7957694669,-72,1,4,0*3F
```

#### 3.1.1 Encryption Enum

| Value | Meaning | ESP32-C3 SDK Constant |
|---|---|---|
| 0 | Open | `WIFI_AUTH_OPEN` |
| 1 | WEP | `WIFI_AUTH_WEP` |
| 2 | WPA_PSK | `WIFI_AUTH_WPA_PSK` |
| 3 | WPA2_PSK | `WIFI_AUTH_WPA2_PSK` |
| 4 | WPA_WPA2_PSK | `WIFI_AUTH_WPA_WPA2_PSK` |
| 5 | WPA2_ENTERPRISE | `WIFI_AUTH_WPA2_ENTERPRISE` |
| 6 | WPA3_PSK | `WIFI_AUTH_WPA3_PSK` |
| 7 | WPA2_WPA3_PSK | `WIFI_AUTH_WPA2_WPA3_PSK` |
| 8 | Unknown | Fallback for unrecognized auth modes |

### 3.2 `$BK` — Batch End

Marks the end of one scan cycle's AP results. Emitted by scan Leaves (W1–W3) after the last `$AP` of a cycle.

```
$BK,leaf_id,count,scan_ms*XX\n
```

| Field | Type | Description |
|---|---|---|
| leaf_id | string | Leaf identifier |
| count | int | Number of `$AP` messages sent in this batch |
| scan_ms | int | Duration of the scan in milliseconds |

**Field count:** 4

Example:
```
$BK,W1,23,142*1A
```

The Branch Controller uses `count` to validate it received all `$AP` messages for the batch. A mismatch between received `$AP` count and the `$BK` count indicates one or more lines were lost to checksum failure or buffer overflow. The Branch Controller logs the discrepancy but does not request retransmission.

### 3.3 `$DE` — Deauth/Disassoc Event

Emitted by the WIDS Leaf (W4) when a deauthentication or disassociation frame is captured in promiscuous mode.

```
$DE,W4,subtype,src,dst,reason,channel,rssi*XX\n
```

| Field | Type | Description |
|---|---|---|
| subtype | int | Frame subtype: `160` (0xA0, disassoc) or `192` (0xC0, deauth) |
| src | MAC | Source MAC address |
| dst | MAC | Destination MAC (`FF:FF:FF:FF:FF:FF` for broadcast) |
| reason | int | 802.11 reason code (1–66+) |
| channel | int | Channel W4 was tuned to at capture time |
| rssi | int | dBm |

**Field count:** 8

Example:
```
$DE,W4,192,DE:AD:BE:EF:00:01,FF:FF:FF:FF:FF:FF,7,6,-45*2B
```

#### 3.3.1 Deauth Flood Detection

The Leaf does NOT perform flood detection — that is the Branch Controller's responsibility. The Leaf emits every deauth/disassoc frame it sees. The Branch Controller applies rate thresholds (e.g., >10 deauth frames from the same source within 5 seconds) to classify floods vs. legitimate disconnections.

Rationale: Keeping detection logic on the Branch Controller allows threshold tuning without reflashing Leaves.

### 3.4 `$PR` — Probe Request

Emitted by the WIDS Leaf (W4) when a probe request frame is captured.

```
$PR,W4,src,ssid_hex,channel,rssi*XX\n
```

| Field | Type | Description |
|---|---|---|
| src | MAC | Client MAC (often randomized on modern devices) |
| ssid_hex | hex | Target SSID the client is probing for (`00` for wildcard/broadcast probe) |
| channel | int | Channel captured on |
| rssi | int | dBm |

**Field count:** 6

Example:
```
$PR,W4,12:34:56:78:9A:BC,486F6D654E6574,6,-41*5E
```

### 3.5 `$BC` — Beacon (WIDS Context)

Emitted by the WIDS Leaf (W4) for beacon frames observed during promiscuous monitoring. Same field format as `$AP`.

```
$BC,W4,BSSID,SSID_hex,RSSI,channel,enc,hidden*XX\n
```

The ESP32-C3 promiscuous callback provides full frame access. The WIDS Leaf parses beacon Information Elements (IEs) to extract SSID (IE tag 0) and encryption (RSN IE tag 48, vendor WPA IE tag 221). This enables the Branch Controller to perform complete evil twin detection: same SSID with different BSSID, same BSSID with different encryption, or same BSSID with different SSID.

**Deduplication:** The WIDS Leaf tracks BSSIDs seen during the current hop cycle using a hash set. A given BSSID is reported via `$BC` at most once per full cycle through the active channel set. The seen set resets at the start of each new cycle. This prevents beacon-rate flooding of the UART (beacons typically arrive every 102.4 ms per AP).

**Seen set capacity:** 512 entries. At ~10 bytes per entry (6-byte MAC + overhead), this consumes ~5 KB — negligible on the C3's 400 KB RAM. If the set fills, `$BC` emission stops for the remainder of the cycle. Deauth and probe events are never suppressed.

**Field count:** 8

### 3.6 `$HB` — Heartbeat

Periodic health report. Emitted by all Leaves.

```
$HB,leaf_id,uptime_s,free_heap,scan_count,err_count*XX\n
```

| Field | Type | Description |
|---|---|---|
| uptime_s | int | Seconds since boot |
| free_heap | int | Free heap in bytes |
| scan_count | int | Scan Leaves: completed scan cycles. WIDS Leaf: completed hop cycles. |
| err_count | int | Cumulative: failed scans + checksum errors on received commands + ring buffer overflows |

**Field count:** 6

**Emission rules:**
- Every 10 seconds during normal operation
- Immediately after boot (before first scan)
- Immediately after processing a `$CF` or `$CH` command (serves as implicit ACK)
- In response to a `$PG` (ping) command

Example:
```
$HB,W1,3247,384512,21583,2*4C
```

---

## 4. Downstream Messages (RP2040 → Leaf)

### 4.1 `$CF` — Configuration

Sets the Leaf's operating mode. Sent once at boot. Can be re-sent at any time to reconfigure.

```
$CF,leaf_id,mode,channel,param1,param2*XX\n
```

| Field | Type | Description |
|---|---|---|
| leaf_id | string | Target Leaf |
| mode | int | `0` = scan, `1` = WIDS |
| channel | int | Scan mode: channel to park on (1–14). WIDS mode: `0` for hop-all (overridden by `$CH` if sent later) |
| param1 | int | WIDS mode: dwell time per channel in ms (default 100). Scan mode: `0` (unused) |
| param2 | int | Reserved, send `0` |

**Field count:** 6

Examples:
```
$CF,W1,0,1,0,0*2E
$CF,W4,1,0,100,0*7A
```

The Leaf responds with `$HB` after applying the configuration.

**Single firmware binary:** All four Leaves run identical firmware. The `$CF` received at boot determines behavior. A Leaf that receives `mode=0,channel=1` becomes a channel-1 scanner. A Leaf that receives `mode=1` becomes the WIDS monitor. This simplifies manufacturing, sparing, and replacement.

### 4.2 `$CH` — Channel Mask (WIDS only)

Dynamically updates the set of channels the WIDS Leaf hops through.

```
$CH,leaf_id,mask*XX\n
```

| Field | Type | Description |
|---|---|---|
| leaf_id | string | Target Leaf (should be W4) |
| mask | int | 14-bit bitmask, bit 0 = channel 1, bit 13 = channel 14 |

**Field count:** 3

Mask values:

| Decimal | Binary | Channels |
|---|---|---|
| 16383 | `11111111111111` | All 1–14 |
| 1057 | `00010000100001` | 1, 6, 11 |
| 8192 | `10000000000000` | 14 only |
| 0 | `00000000000000` | Invalid — Leaf ignores, sends `$HB` with current config unchanged |

The Leaf responds with `$HB` after applying the new mask. If the Leaf is not in WIDS mode (`mode=0`), it ignores `$CH` and responds with `$HB` unchanged.

The WIDS Leaf maintains its current channel mask in memory. On boot, the default is all channels (16383). A `$CF` with `mode=1` resets the mask to all channels. `$CH` overrides.

### 4.3 `$PG` — Ping

```
$PG,leaf_id*XX\n
```

**Field count:** 2

The target Leaf responds with `$HB`. The Branch Controller sends `$PG` if no `$HB` has been received for 15 seconds (missed heartbeat). If no `$HB` response within 3 seconds of `$PG`, the Branch Controller retries once. If still no response, the Leaf is marked offline.

### 4.4 `$RB` — Reboot

```
$RB,leaf_id*XX\n
```

**Field count:** 2

The Leaf immediately calls `esp_restart()`. No response is sent. The Branch Controller watches for the post-boot `$HB` within 5 seconds.

---

## 5. Boot Sequence

### 5.1 Normal Boot (Branch Controller Initiates)

```
Time  RP2040                          Leaf
───── ──────                          ────
0s    Power on, init PIO UARTs
      Start GPS polling
3s    Send $CF,W1,0,1,0,0             W1 boots, UART ready
      Send $CF,W2,0,6,0,0             W2 boots
      Send $CF,W3,0,11,0,0            W3 boots
      Send $CF,W4,1,0,100,0           W4 boots
3.5s                                   W1 receives $CF, applies config
                                       W1 sends $HB (ACK)
                                       W1 begins scanning ch 1
      ... (same for W2, W3, W4)
8s    If no $HB from Wx:
      Retry $CF (up to 3 attempts)
14s   If still no $HB:
      Mark Wx offline
      Continue with remaining Leaves
```

### 5.2 Leaf Standalone Fallback

If a Leaf powers on and receives no `$CF` within 10 seconds:
- Enters scan mode on channel 1
- Begins emitting `$AP`, `$BK`, `$HB` as normal
- Accepts `$CF` at any time to reconfigure

This enables bench testing with a USB-serial adapter without a Branch Controller present.

### 5.3 Leaf Reboot Recovery

If the Branch Controller sends `$RB` or a Leaf crashes and reboots:
- The Leaf boots, sends `$HB` after UART init (~1–2 seconds, faster than ESP8266)
- The Branch Controller detects the `$HB` with `uptime_s` near 0 (fresh boot)
- The Branch Controller re-sends `$CF` (and `$CH` if a non-default mask was active)

---

## 6. Firmware Architecture — Scan Leaf (W1–W3)

### 6.1 State Machine

```
[BOOT] ──(UART ready)──→ [WAIT_CONFIG]
                              │
              ($CF received)  │  (10s timeout)
                    ↓         ↓
              [CONFIGURE] ──→ [CONFIGURE] (default: scan ch 1)
                    │
                    ↓
               [SCANNING] ←──────────────┐
                    │                     │
        (scan complete, send $AP/$BK)     │
                    │                     │
                    ↓                     │
              [IDLE_CHECK]                │
                    │                     │
        (heartbeat due? send $HB)         │
        (command pending? process)        │
                    │                     │
                    └─────────────────────┘
```

### 6.2 Module Decomposition

| Module | Responsibility |
|---|---|
| `main.cpp` | Setup, main loop, state transitions |
| `uart_proto.h/.cpp` | Frame parsing, checksum calc/verify, line assembly, hex SSID encoding |
| `wifi_scan.h/.cpp` | WiFi scan init, result iteration, AP field extraction |
| `config.h/.cpp` | Configuration storage, defaults, mode application |
| `heartbeat.h/.cpp` | Uptime tracking, heap monitoring, error counting, `$HB` emission |
| `cmd_handler.h/.cpp` | Dispatch for incoming `$CF`, `$CH`, `$PG`, `$RB` commands |

### 6.3 Main Loop — Scan Mode

```
setup():
    init UART0 at 230400 (Branch Controller link)
    init WiFi in station mode (no connect)
    send $HB (boot announce)
    start 10s config timeout

loop():
    check_incoming_commands()    // non-blocking UART read

    if state == WAIT_CONFIG:
        if config_timeout_elapsed():
            apply_default_config()  // scan, ch 1
            state = SCANNING

    if state == SCANNING:
        if scan_not_in_progress:
            start_async_scan(assigned_channel)
        if scan_complete:
            for each AP in results:
                send_ap_message(AP)
            send_bk_message(ap_count, scan_duration)
            delete_scan_results()
        if heartbeat_due():
            send_heartbeat()
```

### 6.4 Implementation Notes — Scan Leaf

**Async scanning:** Use `WiFi.scanNetworks(async=true, show_hidden=true, channel=N)`. Blocking scans stall the UART receive path and cause missed commands.

**Scan timing:** Single-channel scans on ESP32-C3 take 100–120 ms. Do not add delays between cycles. The scan function itself provides the pacing.

**AP result lifetime:** Call `WiFi.scanDelete()` after iterating results and before starting the next scan. The ESP32 SDK caches results in heap memory — failure to delete leaks memory.

**SSID extraction:** `WiFi.SSID(i)` returns an Arduino String. Convert each byte to two hex chars. Handle the case where SSID length is 0 (hidden network) — emit `00`.

**BSSID extraction:** `WiFi.BSSIDstr(i)` returns `AA:BB:CC:DD:EE:FF` format. Use directly.

**Encryption extraction:** `WiFi.encryptionType(i)` returns `wifi_auth_mode_t`. Map directly to the enum in §3.1.1. Values 6 and 7 (WPA3) are natively supported.

**UART TX:** At 230400 baud, a 200-byte line takes ~8.7 ms to transmit. With 40+ APs per scan in dense urban, a batch takes ~350 ms. The ESP32-C3 UART TX FIFO handles this without software buffering. Use `Serial.availableForWrite()` as a flow control check before each line — if the FIFO is full, yield briefly.

**UART RX handling:** Process incoming bytes every loop iteration. Accumulate into a 200-byte line buffer. Parse on `\n`. If `$` is received mid-accumulation (framing error recovery), discard current buffer and restart from `$`.

---

## 7. Firmware Architecture — WIDS Leaf (W4)

### 7.1 State Machine

Same as Scan Leaf through [CONFIGURE]. After configuration:

```
[CONFIGURE] ──→ [WIDS_HOPPING]
                      │
                      ↓
              [SET_CHANNEL] ←─────────────────┐
                      │                        │
          (enable promiscuous mode)            │
                      │                        │
                      ↓                        │
              [DWELL] ─── (callback fires ──→ write to ring buffer)
                      │                        │
          (dwell time elapsed)                 │
                      │                        │
                      ↓                        │
              [DRAIN_AND_HOP]                  │
                      │                        │
          (drain ring buffer → emit $DE/$PR/$BC)
          (more channels in mask? ────────────┘
           no more → cycle complete,
           reset seen-BSSID set,
           send $HB if due,
           restart from first channel)
```

### 7.2 Additional Modules (beyond §6.2)

| Module | Responsibility |
|---|---|
| `wids_monitor.h/.cpp` | Promiscuous mode setup/teardown, channel hopping, dwell timer |
| `frame_parser.h/.cpp` | 802.11 header decoding: type, subtype, addresses, reason codes, beacon IE parsing |
| `bssid_tracker.h/.cpp` | Per-cycle BSSID hash set for `$BC` deduplication |

### 7.3 Promiscuous Callback

Register via `esp_wifi_set_promiscuous_rx_cb()`. The callback receives a `wifi_promiscuous_pkt_t` struct containing:
- `rx_ctrl`: RSSI, channel, rate, sig_mode, timestamp
- `payload`: Raw 802.11 frame bytes

**Critical rule: The callback runs in the WiFi task context, not the Arduino loop(). Do NOT call Serial, WiFi, or any non-reentrant function from inside the callback. Write to the ring buffer only.**

This is the single most important implementation constraint. Violating it causes watchdog resets and data corruption.

### 7.4 Ring Buffer

```c
struct WidsEvent {
    uint8_t  type;           // EVENT_DEAUTH, EVENT_PROBE, EVENT_BEACON
    uint8_t  subtype;        // 802.11 frame subtype (deauth/disassoc distinction)
    uint8_t  src[6];         // source MAC
    uint8_t  dst[6];         // destination MAC
    uint8_t  bssid[6];       // BSSID
    uint8_t  ssid[32];       // SSID bytes (probes and beacons)
    uint8_t  ssid_len;       // actual SSID length (0 = hidden/broadcast)
    int8_t   rssi;           // dBm
    uint8_t  channel;        // channel at capture
    uint16_t reason;         // reason code (deauth/disassoc only)
    uint8_t  enc;            // encryption type (beacons only)
    uint8_t  hidden;         // hidden SSID flag (beacons only)
};
```

**Capacity:** 64 slots. At ~56 bytes per event ≈ 3.5 KB. Trivial on 400 KB RAM.

**Concurrency model:** Single producer (promiscuous callback), single consumer (main loop). Use a volatile write index and a non-volatile read index. No mutex needed for SPSC on a single-core MCU (the C3 is single-core RISC-V).

**Overflow:** If write index catches read index, the oldest event is overwritten. The main loop increments `err_count` when it detects the gap. This is acceptable — transient event loss during high-activity bursts is preferable to blocking the WiFi task.

### 7.5 Beacon IE Parsing

The ESP32-C3 provides full frame access in the promiscuous callback. Beacon frame body starts at byte offset 36 (after MAC header + fixed fields). IEs are TLV-encoded:

```
Tag (1 byte) | Length (1 byte) | Value (Length bytes)
```

Required IEs to parse:

| Tag | Name | Purpose |
|---|---|---|
| 0 | SSID | Network name (length 0 = hidden) |
| 48 | RSN (Robust Security Network) | WPA2/WPA3 detection: parse AKM suite list |
| 221 | Vendor Specific | WPA1 detection: check OUI `00:50:F2:01` |

**RSN AKM suite OID mapping for encryption enum:**

| AKM Suite (last byte) | Encryption |
|---|---|
| 1 | WPA2_ENTERPRISE (802.1X) |
| 2 | WPA2_PSK |
| 8 | SAE (WPA3_PSK) |
| 18 | OWE (Enhanced Open) |

If both WPA2 PSK and SAE AKM suites are present → `enc=7` (WPA2_WPA3_PSK).

Implementation: Write a linear IE walker that scans from offset 36 to end of frame. Extract SSID bytes from tag 0. For tags 48 and 221, extract the AKM suite type and map to the encryption enum. If neither RSN nor vendor WPA IE is found, check the Privacy bit in the Capability Info field (offset 34, bit 4) — if set, report `enc=1` (WEP); if clear, report `enc=0` (Open).

**Parsing is done inside the callback** to populate the `WidsEvent` struct before writing it to the ring buffer. The IE walker must be fast and must not allocate heap. Pre-compute the frame body pointer and length from `rx_ctrl.sig_len` and iterate with pointer arithmetic.

### 7.6 Channel Hopping

```
active_channels[] built from $CH mask (or all 14 by default)
channel_index = 0

hop_cycle():
    for channel_index in 0..active_channel_count:
        channel = active_channels[channel_index]
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE)
        dwell_start = millis()

        while (millis() - dwell_start < dwell_time_ms):
            drain_ring_buffer()      // emit $DE, $PR, $BC from buffered events
            check_incoming_commands() // process $CH, $PG, $RB
            yield()

    // Cycle complete
    clear_seen_bssid_set()
    increment_scan_count()
    if heartbeat_due():
        send_heartbeat()
```

Full cycle through 14 channels at 100 ms dwell = 1.4 seconds per sweep. Narrowing via `$CH` proportionally reduces cycle time.

### 7.7 BSSID Dedup — Seen Set

Hash set of BSSIDs observed during the current hop cycle. 512-entry capacity.

Implementation: Fixed-size open-addressing hash table. Key = 6-byte MAC. Hash = FNV-1a over the 6 bytes, modulo table size. Linear probing on collision. No deletion — the entire table is cleared at the end of each cycle.

On `EVENT_BEACON` in the ring buffer drain:
1. Check if BSSID exists in seen set
2. If yes, discard (do not emit `$BC`)
3. If no, insert into seen set and emit `$BC`
4. If table is full, stop inserting but still do the lookup (may match existing entries). If not found and table full, discard silently and increment `err_count`.

---

## 8. Error Handling

### 8.1 Checksum Failure (Receiver Side)

Discard the line. Increment `err_count`. No retransmission. No NAK.

### 8.2 Unknown Message Type

Increment `err_count`, discard.

### 8.3 Field Count Mismatch

Discard. Increment `err_count`.

### 8.4 Scan Failure

`WiFi.scanNetworks()` can return negative error codes (`-1` busy, `-2` fail). On failure:
- Do not send `$BK` — skip the batch entirely
- Increment `err_count`
- Retry scan on next loop iteration
- If 10 consecutive failures, call `WiFi.disconnect()` + `WiFi.mode(WIFI_STA)` to reset the radio
- If 50 consecutive failures, self-reboot (`esp_restart()`)

### 8.5 Heap Exhaustion

Monitor `esp_get_free_heap_size()` every heartbeat cycle. If free heap drops below 16384 bytes:
- Skip current scan cycle
- Call `WiFi.scanDelete()` to free cached results
- If heap does not recover within 3 heartbeat cycles, self-reboot

### 8.6 UART TX Flow Control

If `Serial.availableForWrite()` returns 0, spin-wait up to 50 ms. If still blocked, discard the current message (not the whole batch) and increment `err_count`.

---

## 9. Configuration Defaults

If no `$CF` is received within 10 seconds of boot:

| Parameter | Default |
|---|---|
| mode | 0 (scan) |
| channel | 1 |
| dwell_time_ms | 100 (WIDS only) |
| channel_mask | 16383 (all 1–14, WIDS only) |
| heartbeat_interval_s | 10 |

Defaults are compiled in, not stored in flash. Every boot starts from defaults, then `$CF` from the Branch Controller overrides.

---

## 10. Build Configuration

### 10.1 PlatformIO Project

```ini
[env:leaf_wifi24]
platform = espressif32
board = seeed_xiao_esp32c3
framework = arduino
monitor_speed = 230400
build_flags =
    -DLEAF_VERSION=\"1.1.0\"
    -DMAX_LINE_LEN=200
    -DHB_INTERVAL_MS=10000
    -DCONFIG_TIMEOUT_MS=10000
    -DSCAN_FAIL_REBOOT_THRESHOLD=50
    -DHEAP_MIN_BYTES=16384
    -DWIDS_RING_SIZE=64
    -DWIDS_SEEN_BSSID_MAX=512
```

Board target may change depending on exact C3 module purchased (e.g., `esp32-c3-devkitm-1`, `esp32-c3-devkitc-02`, `seeed_xiao_esp32c3`). The firmware is board-agnostic — only UART pin mapping changes.

### 10.2 Source Tree

```
leaf_wifi24/
├── platformio.ini
├── src/
│   ├── main.cpp              # setup(), loop(), state machine
│   ├── uart_proto.h          # Frame building, checksum, hex encoding
│   ├── uart_proto.cpp
│   ├── wifi_scan.h           # Async scan wrapper, result extraction
│   ├── wifi_scan.cpp
│   ├── wids_monitor.h        # Promiscuous mode, channel hopping, ring buffer
│   ├── wids_monitor.cpp
│   ├── frame_parser.h        # 802.11 header + beacon IE parsing
│   ├── frame_parser.cpp
│   ├── bssid_tracker.h       # FNV-1a hash set for BSSID dedup
│   ├── bssid_tracker.cpp
│   ├── config.h              # Configuration state and defaults
│   ├── config.cpp
│   ├── heartbeat.h           # Health monitoring and $HB emission
│   ├── heartbeat.cpp
│   ├── cmd_handler.h         # Downstream command dispatch ($CF, $CH, $PG, $RB)
│   └── cmd_handler.cpp
└── include/
    └── leaf_defs.h           # Shared constants, enums, struct definitions
```

---

## 11. Testing Procedure

### 11.1 Bench Test — Single Leaf

Equipment: USB-serial adapter (3.3V logic), serial terminal at 230400.

1. Flash firmware to ESP32-C3, power on
2. Observe `$HB` within 2 seconds of boot
3. Wait 10 seconds — Leaf enters default scan mode (ch 1)
4. Observe `$AP` and `$BK` messages streaming
5. Verify checksum on several messages manually
6. Verify WPA3 networks report `enc=6` or `enc=7` (if WPA3 APs available)
7. Send `$CF,W1,0,6,0,0*XX` — Leaf responds with `$HB`, switches to ch 6
8. Send `$CF,W1,1,0,100,0*XX` — Leaf enters WIDS mode, observe `$DE`/`$PR`/`$BC`
9. Verify `$BC` messages include SSID and correct encryption (not `Unknown`)
10. Send `$CH,W1,2048*XX` (channel 12 only) — observe WIDS activity narrowed to ch 12
11. Send `$PG,W1*XX` — observe `$HB` response
12. Send `$RB,W1*XX` — Leaf reboots, observe fresh `$HB` with `uptime_s` near 0

### 11.2 Integration Test — Full Branch

Equipment: RP2040 + 4× ESP32-C3 Leaves wired per §1 topology.

1. Power on assembly
2. RP2040 sends `$CF` to all four Leaves
3. Verify all four respond with `$HB` within 5 seconds
4. Monitor RP2040 aggregate output on STM32 UART — verify detection records are GPS-timestamped and attributed to correct Leaves
5. Kill power to W2 — verify RP2040 marks W2 offline after missed heartbeat + ping timeout
6. Restore power to W2 — verify RP2040 detects fresh `$HB` and re-sends `$CF`
7. Send `$CH` via RP2040 to W4 targeting channels 1, 6, 11 only — verify reduced hop set
8. In a dense environment (>30 APs), verify no `$BK` count mismatches (all `$AP` lines received)
9. Trigger a deauth event (test tool or second device) — verify `$DE` appears from W4

---

## 12. 802.11 Frame Reference

### 12.1 Frame Control Field

First 2 bytes of every 802.11 frame (little-endian):

```
Byte 0:
  Bits 0-1:  Protocol version (always 0)
  Bits 2-3:  Type (0=management, 1=control, 2=data)
  Bits 4-7:  Subtype

Byte 1:
  Bit 0:     To DS
  Bit 1:     From DS
  Bits 2-7:  More fragments, retry, power mgmt, more data, protected, order
```

### 12.2 Management Frame Subtypes

| Subtype | FC Byte 0 | Frame | WIDS Action |
|---|---|---|---|
| 0x00 | 0x00 | Association Request | Ignore |
| 0x01 | 0x10 | Association Response | Ignore |
| 0x04 | 0x40 | Probe Request | Emit `$PR` |
| 0x05 | 0x50 | Probe Response | Ignore (redundant with beacons for AP detection) |
| 0x08 | 0x80 | Beacon | Emit `$BC` (deduplicated) |
| 0x0A | 0xA0 | Disassociation | Emit `$DE` |
| 0x0C | 0xC0 | Deauthentication | Emit `$DE` |

### 12.3 Management Frame Address Layout

```
Offset 4–9:   Address 1 (Destination / Receiver)
Offset 10–15: Address 2 (Source / Transmitter)
Offset 16–21: Address 3 (BSSID)
Offset 22–23: Sequence Control
```

### 12.4 Deauth/Disassoc Reason Code

2 bytes, little-endian, at offset 24.

Common values:

| Code | Meaning |
|---|---|
| 1 | Unspecified |
| 2 | Auth no longer valid |
| 3 | Deauth leaving BSS |
| 4 | Disassoc due to inactivity |
| 6 | Class 2 frame from non-auth station |
| 7 | Class 3 frame from non-assoc station |

### 12.5 Beacon Frame Body

Starts at offset 36 (after 24-byte MAC header + 8 bytes timestamp + 2 bytes interval + 2 bytes capability).

**Capability Info** (offset 34–35): Bit 4 (Privacy) indicates encryption is in use.

IEs are contiguous TLV entries from offset 36 to end of frame:

```
[Tag: 1 byte] [Length: 1 byte] [Value: Length bytes]
```

Walk with:
```
ptr = frame + 36
end = frame + frame_len
while (ptr + 2 <= end):
    tag = ptr[0]
    len = ptr[1]
    if (ptr + 2 + len > end): break  // malformed, stop
    process_ie(tag, ptr + 2, len)
    ptr += 2 + len
```

---

## 13. Open Items

| # | Item | Status |
|---|------|--------|
| 1 | RP2040 Branch Controller firmware guide | Next |
| 2 | RP2040 → STM32 upstream message format | Not yet specified |
| 3 | RP2040 GPS integration (NMEA parsing from BN-220) | Not yet specified |
| 4 | RP2040 evil twin detection logic | Not yet specified |
| 5 | RP2040 deauth flood classification thresholds | Not yet specified |
| 6 | Final RP2040 pin assignments pending PCB layout | Preliminary in §1 |
| 7 | Power regulation for 4× ESP32-C3 from Branch Controller | Not yet analyzed |
| 8 | Exact ESP32-C3 module selection (XIAO, SuperMini, DevKitM, etc.) | Pending order |
