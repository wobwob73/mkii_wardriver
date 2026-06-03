# 2.4 GHz WiFi Branch — RP2040 Branch Controller Firmware Guide

**Version:** 1.0.0
**Date:** 2026-04-04
**Scope:** RP2040 Branch Controller firmware for the 2.4 GHz WiFi Branch
**Dependencies:** Leaf Protocol Spec v1.1 (ESP32-C3)

---

## 1. Hardware Interface Summary

```
                        RP2040
                  ┌───────────────────┐
  Leaf W1 TX ───→ │ GP1  (PIO0 SM0 RX)│
  Leaf W1 RX ←─── │ GP0  (PIO0 SM0 TX)│
  Leaf W2 TX ───→ │ GP3  (PIO0 SM1 RX)│
  Leaf W2 RX ←─── │ GP2  (PIO0 SM1 TX)│
  Leaf W3 TX ───→ │ GP5  (PIO0 SM2 RX)│
  Leaf W3 RX ←─── │ GP4  (PIO0 SM2 TX)│
  Leaf W4 TX ───→ │ GP7  (PIO0 SM3 RX)│
  Leaf W4 RX ←─── │ GP6  (PIO0 SM3 TX)│
                  │                   │
  STM32 TX ────→  │ GP13 (UART0 RX)   │
  STM32 RX ←────  │ GP12 (UART0 TX)   │
                  │                   │
  1PPS ─────────→ │ GP10 (GPIO IRQ)    │
                  │                   │
  (debug) ←─────  │ GP16 (UART1 TX)   │  optional: debug log output
                  └───────────────────┘
```

| Interface | Peripheral | Baud/Config | Purpose |
|---|---|---|---|
| 4× Leaf UARTs | PIO0 SM0–SM3 | 230400 8N1 | Leaf communication (bidirectional) |
| STM32 upstream | UART0 | 230400 8N1 | Aggregated detection stream + receive $TM |
| 1PPS input | GPIO10 interrupt | Rising edge | Sub-second timestamp synchronization |
| Debug output | UART1 (optional) | 115200 8N1 | Development logging, disabled in production |

Pin assignments are preliminary — subject to PCB layout.

---

## 2. Dual-Core Architecture

### 2.1 Core Assignment

| Core | Responsibilities |
|---|---|
| **Core 0** | PIO UART management (4 Leaf links). Leaf message parsing and validation. Leaf health monitoring (heartbeat tracking, ping, reboot). Downstream command transmission ($CF, $CH, $PG, $RB). Writes parsed records into shared inter-core queues. |
| **Core 1** | 1PPS interrupt handling and time maintenance. $TM parsing from STM32 UART0 RX. AP deduplication. WIDS analysis (evil twin detection, deauth flood classification). Timestamp injection. Upstream message formatting and transmission on UART0 TX. |

### 2.2 Rationale

Core 0 is latency-sensitive — it must keep up with 4 simultaneous UART streams at 230400 baud without dropping bytes. Isolating it from the upstream TX path (which can block briefly on UART FIFO) and from WIDS analysis (which does hash table lookups and threshold checks) ensures Leaf data is never lost due to processing delays on the output side.

Core 1 is throughput-sensitive — it must deduplicate, analyze, timestamp, and serialize the aggregated output. It has exclusive ownership of the upstream UART TX, so there is no contention on the output path.

---

## 3. Timing — 1PPS Synchronization

### 3.1 Hardware Setup

The STM32's GPS module (M10Q-5883) provides a 1PPS output: one rising edge per UTC second boundary, typically ±30 ns accuracy. This signal is wired to RP2040 GPIO10.

The RP2040 captures PPS edges using a GPIO interrupt that reads the hardware timer (`time_us_64()`). This gives a microsecond-resolution local timestamp for each UTC second boundary.

### 3.2 Time Epoch from STM32

The 1PPS edge tells the RP2040 *when* a second boundary occurred but not *which* second. The STM32 provides this via a `$TM` message sent on the Branch UART after each PPS:

```
$TM,epoch_s,fix_ok*XX\n
```

| Field | Type | Description |
|---|---|---|
| epoch_s | int | UTC epoch seconds corresponding to the most recent PPS rising edge |
| fix_ok | int | `1` if GPS has valid fix, `0` if no fix (epoch_s may be stale or estimated) |

**Field count:** 3

The STM32 sends `$TM` within 100 ms of each PPS edge (after it processes the GPS solution). The RP2040 associates the received `epoch_s` with the most recent PPS capture timestamp.

### 3.3 Time State

```c
struct TimeState {
    uint64_t pps_timer_us;      // time_us_64() at last PPS rising edge
    uint32_t epoch_s;           // UTC epoch second from last $TM
    bool     time_valid;        // true if epoch_s has been received and PPS is active
    bool     fix_ok;            // true if STM32 GPS has valid fix
    uint64_t last_pps_age_us;   // time since last PPS edge (for stale detection)
};
```

Updated by Core 1. Read by Core 0 (for timestamping records before queue insertion — see §4.2 alternative) or read by Core 1 at dequeue time.

**Access pattern:** Core 1 writes atomically (single writer). Core 1 reads for timestamp computation. Core 0 does not read TimeState directly — timestamps are applied by Core 1 at dequeue time.

### 3.4 Timestamp Computation

For any event occurring at local timer value `event_us`:

```
offset_us = event_us - pps_timer_us
timestamp_s = epoch_s + (offset_us / 1_000_000)
timestamp_frac_us = offset_us % 1_000_000
```

Output format for upstream messages: `epoch_s.fraction` as a fixed-point decimal with 6 fractional digits.

Example: `1712188800.142857` = April 4, 2026, 00:00:00.142857 UTC.

### 3.5 Clock Drift Between PPS Edges

RP2040 crystal oscillator: 12 MHz, ±20 ppm typical. Over 1 second, drift is ±20 µs. At 120 mph (54 m/s), this is ~1 mm of positional error. No correction needed.

If the PPS signal is lost (no edge for >2 seconds), the RP2040 continues free-running with the last known epoch, incrementing `epoch_s` once per second using the internal timer. A `degraded` flag is set on upstream records to indicate free-running time. Accuracy degrades at ~20 µs/s — after 60 seconds without PPS, drift is ~1.2 ms (~6.5 cm at 120 mph). Still acceptable.

### 3.6 PPS Loss Detection and Recovery

```
PPS_TIMEOUT_US = 2_000_000   // 2 seconds

if (time_us_64() - pps_timer_us > PPS_TIMEOUT_US):
    time_valid = false   // timestamps will carry degraded flag
    // Continue incrementing epoch_s based on internal timer
    // $TM messages are ignored until PPS resumes

on PPS edge received after timeout:
    pps_timer_us = captured value
    // Wait for next $TM to re-sync epoch_s
    // Do NOT immediately mark time_valid — wait for $TM confirmation
```

---

## 4. Inter-Core Communication

### 4.1 Shared Queues

Two lock-free SPSC (single-producer, single-consumer) ring buffers, protected by RP2040 hardware spinlocks for the rare case of simultaneous access to head/tail pointers.

| Queue | Producer | Consumer | Element Type | Capacity |
|---|---|---|---|---|
| Detection Queue | Core 0 | Core 1 | `DetectionRecord` | 256 slots |
| WIDS Queue | Core 0 | Core 1 | `WidsRecord` | 128 slots |

### 4.2 Detection Record

Written by Core 0 when a valid `$AP` is parsed from any scan Leaf.

```c
struct DetectionRecord {
    uint64_t local_timer_us;    // time_us_64() at parse completion
    uint8_t  leaf_id;           // 1–4
    uint8_t  bssid[6];
    uint8_t  ssid[32];
    uint8_t  ssid_len;
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  enc;
    uint8_t  hidden;
};
```

~48 bytes per record. 256 slots = ~12 KB.

Core 0 captures `local_timer_us` at the moment parsing completes. Core 1 converts this to a UTC timestamp using the TimeState when dequeuing.

### 4.3 WIDS Record

Written by Core 0 when a valid `$DE`, `$PR`, or `$BC` is parsed from W4.

```c
struct WidsRecord {
    uint64_t local_timer_us;
    uint8_t  type;              // WIDS_DEAUTH, WIDS_PROBE, WIDS_BEACON
    uint8_t  subtype;           // frame subtype (deauth vs disassoc)
    uint8_t  src[6];
    uint8_t  dst[6];
    uint8_t  bssid[6];
    uint8_t  ssid[32];
    uint8_t  ssid_len;
    int8_t   rssi;
    uint8_t  channel;
    uint16_t reason;
    uint8_t  enc;
    uint8_t  hidden;
};
```

~64 bytes per record. 128 slots = ~8 KB.

### 4.4 Overflow Handling

If a queue is full when Core 0 attempts to write, the record is discarded and an overflow counter is incremented. This counter is reported in the Branch status heartbeat upstream. Core 0 never blocks.

---

## 5. Core 0 — Leaf I/O

### 5.1 PIO UART Setup

Each PIO state machine runs a standard UART RX/TX program. The RP2040 SDK provides `uart_rx` and `uart_tx` PIO programs. Each SM is configured for 230400 baud.

DMA is used for RX on all 4 PIO UARTs. Each RX channel DMAs into a per-Leaf circular byte buffer (512 bytes each). Core 0 polls these buffers for complete lines.

TX is polled (non-DMA) — downstream messages to Leaves are infrequent and short.

### 5.2 Line Assembly

Each Leaf has an independent line assembler:

```c
struct LeafRxState {
    uint8_t  line_buf[200];
    uint8_t  line_pos;
    bool     in_frame;          // true after seeing '$'
};
```

Byte-by-byte processing from the DMA ring buffer:
1. On `$`: set `in_frame = true`, reset `line_pos = 0`, store `$`
2. On `\n` while `in_frame`: terminate line, validate checksum, dispatch to parser
3. On `line_pos >= 200`: discard (line too long), reset state
4. Otherwise: append byte to `line_buf`

### 5.3 Message Dispatch

After checksum validation, Core 0 inspects the message type (bytes 1–2 after `$`):

| Type | Action |
|---|---|
| `AP` | Parse fields → write `DetectionRecord` to Detection Queue |
| `BK` | Update Leaf batch tracking (expected vs received AP count) |
| `DE` | Parse fields → write `WidsRecord` (type=DEAUTH) to WIDS Queue |
| `PR` | Parse fields → write `WidsRecord` (type=PROBE) to WIDS Queue |
| `BC` | Parse fields → write `WidsRecord` (type=BEACON) to WIDS Queue |
| `HB` | Update Leaf health state (uptime, heap, errors). Reset watchdog timer for that Leaf. |

### 5.4 Leaf Health State

```c
struct LeafState {
    uint8_t  id;                // 1–4
    bool     online;
    bool     configured;
    uint32_t last_hb_time_ms;   // millis() of last heartbeat
    uint32_t uptime_s;          // from last $HB
    uint32_t free_heap;         // from last $HB
    uint32_t scan_count;        // from last $HB
    uint32_t err_count;         // from last $HB
    uint32_t ap_expected;       // from last $BK count field
    uint32_t ap_received;       // count of $AP messages since last $BK
    uint32_t batch_mismatches;  // cumulative $BK count != received $AP count
    uint8_t  ping_retries;      // current ping retry count
    uint8_t  cf_retries;        // current $CF retry count
};
```

### 5.5 Leaf Watchdog

Runs every 1 second on Core 0:

```
for each Leaf:
    if not online: skip
    age = millis() - last_hb_time_ms

    if age > 15000 and ping_retries == 0:
        send $PG to Leaf
        ping_retries = 1

    if age > 18000 and ping_retries == 1:
        send $PG to Leaf (retry)
        ping_retries = 2

    if age > 21000 and ping_retries == 2:
        mark Leaf offline
        log event
        // Do NOT auto-reboot — a crashed Leaf may reboot itself.
        // If it comes back, its $HB with uptime_s near 0 triggers
        // re-configuration (see §5.6).
```

### 5.6 Leaf Recovery Detection

When a `$HB` arrives from a Leaf marked offline, or from any Leaf with `uptime_s < 5` (fresh boot):
1. Mark Leaf as online
2. Re-send `$CF` with the Leaf's assigned configuration
3. If the Leaf is W4 and a non-default `$CH` mask was active, re-send `$CH` after `$CF`
4. Reset ping retry counter

### 5.7 Boot Orchestration

At startup, Core 0 executes the Leaf initialization sequence:

```
wait 3 seconds (Leaf boot time)

for each Leaf W1–W4:
    send $CF with assigned config
    cf_retries = 0

// Then in the main loop, the watchdog handles $HB tracking.
// If $HB not received within 5 seconds of $CF:
//   retry $CF up to 3 times at 2-second intervals
//   if still no $HB, mark offline
```

### 5.8 Core 0 Main Loop

```
core0_main():
    init_pio_uarts()
    init_leaf_states()
    wait(3000)              // Leaf boot time
    send_initial_cf()

    while true:
        // Service all 4 Leaf RX DMA buffers
        for each leaf in 0..3:
            while bytes_available(leaf):
                byte = read_byte(leaf)
                assemble_line(leaf, byte)
                if line_complete:
                    if checksum_valid:
                        dispatch_message(leaf, line)
                    else:
                        leaf_state[leaf].err_count++

        // Leaf watchdog (1 Hz)
        if watchdog_tick_due:
            run_leaf_watchdog()

        // Process any downstream commands queued by Core 1
        // (e.g., $CH mask changes relayed from STM32)
        if downstream_cmd_pending:
            send_downstream_cmd()
```

---

## 6. Core 1 — Time, Analysis, and Upstream

### 6.1 1PPS Interrupt Handler

```c
void pps_isr(void) {
    time_state.pps_timer_us = time_us_64();
    time_state.pps_received = true;  // flag for main loop
}
```

Registered on GPIO10 rising edge. Minimal work — just capture the timer. Epoch association happens in the main loop when `$TM` arrives.

### 6.2 $TM Processing

Core 1 reads UART0 RX for messages from the STM32. When `$TM` is received:

```
if time_state.pps_received:
    time_state.epoch_s = parsed_epoch_s
    time_state.fix_ok = parsed_fix_ok
    time_state.time_valid = true
    time_state.pps_received = false
```

`$TM` is only meaningful if a PPS edge preceded it. If `$TM` arrives without a recent PPS edge (>1.5 seconds since last PPS), it is logged but not applied — the RP2040 cannot associate an epoch with a PPS it didn't see.

### 6.3 AP Deduplication

Scan Leaves on channels 1, 6, and 11 have overlapping reception. An AP on channel 3 will appear in W1 and W2 results. An AP on channel 9 will appear in W2 and W3 results. Deduplication ensures each AP is reported upstream once per time window.

**Dedup table:** Hash map keyed by BSSID (6 bytes). Each entry stores:

```c
struct DedupEntry {
    uint8_t  bssid[6];
    int8_t   best_rssi;        // strongest RSSI seen in current window
    uint8_t  best_leaf;        // which Leaf reported the best RSSI
    uint8_t  ssid[32];
    uint8_t  ssid_len;
    uint8_t  channel;          // AP's reported channel (from beacon, consistent across Leaves)
    uint8_t  enc;
    uint8_t  hidden;
    uint64_t local_timer_us;   // timestamp of best-RSSI observation
    uint64_t window_start_us;  // start of current dedup window
    bool     emitted;          // true if already sent upstream this window
};
```

**Window:** 500 ms. This aligns roughly with 3–5 scan cycles per Leaf (100–150 ms each). Within a window, the same BSSID from multiple Leaves is merged — the strongest RSSI wins.

**Flush:** At the end of each window, all entries with `emitted == false` are formatted as upstream `$WA` messages and transmitted. The `emitted` flag is then set. Entries not seen in the subsequent window are evicted.

**Table size:** 512 entries. At ~52 bytes each ≈ 26 KB. RP2040 has 264 KB SRAM — this is fine.

**Collision handling:** Open-addressing with linear probing, same as the Leaf BSSID tracker. FNV-1a hash on the 6-byte BSSID.

### 6.4 WIDS Analysis

Core 1 drains the WIDS Queue and performs two analyses:

#### 6.4.1 Evil Twin Detection

Maintains a known-AP table built from `$AP` data (via the dedup table in §6.3). When a `$BC` (beacon from WIDS Leaf) arrives:

1. Look up BSSID in the dedup table
2. **Same BSSID, different SSID:** Evil twin alert — a rogue AP is impersonating a known network's BSSID but broadcasting a different name. Emit `$ET`.
3. **Same SSID, different BSSID, different encryption:** Suspicious — could be an evil twin using a cloned SSID but its own MAC and weaker security. Emit `$ET`.
4. **Same SSID, different BSSID, same encryption:** Legitimate — multiple APs with the same SSID (enterprise/mesh networks). No alert.
5. **BSSID not in dedup table:** New AP seen only by WIDS Leaf (e.g., on a non-standard channel). Forward as a regular detection upstream.

```c
struct EvilTwinAlert {
    uint64_t timestamp_us;
    uint8_t  rogue_bssid[6];
    uint8_t  known_bssid[6];     // the legitimate BSSID if applicable
    uint8_t  ssid[32];
    uint8_t  ssid_len;
    uint8_t  rogue_enc;
    uint8_t  known_enc;
    int8_t   rogue_rssi;
    uint8_t  channel;
    uint8_t  alert_type;         // SSID_MISMATCH, ENC_MISMATCH
};
```

#### 6.4.2 Deauth Flood Detection

Maintains a per-source-MAC event counter with a sliding window:

```c
struct DeauthTracker {
    uint8_t  src[6];
    uint16_t count;             // events in current window
    uint64_t window_start_us;
};
```

**Threshold:** >10 deauth/disassoc frames from the same source MAC within 5 seconds → emit `$DF` (deauth flood alert).

**Tracker capacity:** 64 entries. If full, oldest entry is evicted (LRU by window_start). In practice, a deauth flood comes from 1–2 source MACs, so 64 is generous.

**Reset:** After emitting `$DF`, the counter for that source resets. A sustained flood generates one `$DF` per 5-second window, not per frame.

Individual `$DE` events are NOT forwarded upstream as raw records. The Branch Controller absorbs them and emits only the processed alert. This prevents a deauth flood from saturating the upstream UART. Probe requests (`$PR`) are forwarded individually — they don't have the same flood characteristics.

### 6.5 Core 1 Main Loop

```
core1_main():
    init_uart0()            // STM32 upstream
    init_pps_interrupt()
    init_dedup_table()
    init_wids_tables()

    while true:
        // 1. Check for $TM from STM32
        if uart0_rx_available:
            parse_stm32_message()   // $TM or future commands

        // 2. PPS health check
        update_pps_state()          // detect loss, manage free-running fallback

        // 3. Drain Detection Queue
        while detection_queue.readable():
            record = detection_queue.read()
            dedup_table.update(record)

        // 4. Flush dedup window if expired
        if dedup_window_expired():
            for each entry in dedup_table where !emitted:
                timestamp = compute_timestamp(entry.local_timer_us)
                send_upstream_wa(entry, timestamp)
                entry.emitted = true
            evict_stale_entries()
            advance_window()

        // 5. Drain WIDS Queue
        while wids_queue.readable():
            record = wids_queue.read()
            timestamp = compute_timestamp(record.local_timer_us)

            switch record.type:
                case BEACON:
                    check_evil_twin(record)
                case DEAUTH:
                    update_deauth_tracker(record)
                case PROBE:
                    send_upstream_wp(record, timestamp)

        // 6. Check deauth flood thresholds
        check_flood_thresholds()

        // 7. Branch heartbeat (every 10 seconds)
        if branch_hb_due():
            send_upstream_bs()
```

---

## 7. Upstream Messages (RP2040 → STM32)

Same NMEA-style framing as the Leaf protocol (§2 of Leaf spec). Checksum, line length, and receiver behavior rules are identical.

### 7.1 `$WA` — WiFi AP Detection (Deduplicated)

```
$WA,branch_id,timestamp,BSSID,SSID_hex,RSSI,channel,enc,hidden,leaf_id,time_flag*XX\n
```

| Field | Type | Description |
|---|---|---|
| branch_id | string | `W24` (2.4 GHz WiFi Branch identifier) |
| timestamp | float | UTC epoch with 6 decimal places (e.g., `1712188800.142857`) |
| BSSID | MAC | AP MAC address |
| SSID_hex | hex string | Hex-encoded SSID |
| RSSI | int | Best RSSI from dedup window (dBm) |
| channel | int | AP's reported operating channel |
| enc | int | Encryption enum (same as Leaf spec §3.1.1) |
| hidden | int | 0 or 1 |
| leaf_id | string | Leaf that reported the best RSSI (`W1`–`W3`) |
| time_flag | int | `0` = PPS-synced, `1` = degraded (free-running clock) |

**Field count:** 11

Example:
```
$WA,W24,1712188800.142857,A0:B1:C2:D3:E4:F5,4D7957694669,-67,3,4,0,W1,0*5A
```

### 7.2 `$WP` — Probe Request (Forwarded)

```
$WP,W24,timestamp,src,ssid_hex,channel,rssi,time_flag*XX\n
```

| Field | Type | Description |
|---|---|---|
| branch_id | string | `W24` |
| timestamp | float | UTC epoch, 6 decimal places |
| src | MAC | Client MAC |
| ssid_hex | hex | Target SSID (`00` for broadcast probe) |
| channel | int | Channel captured on |
| rssi | int | dBm |
| time_flag | int | 0 = synced, 1 = degraded |

**Field count:** 8

### 7.3 `$ET` — Evil Twin Alert

```
$ET,W24,timestamp,alert_type,rogue_bssid,known_bssid,ssid_hex,rogue_enc,known_enc,rogue_rssi,channel,time_flag*XX\n
```

| Field | Type | Description |
|---|---|---|
| branch_id | string | `W24` |
| timestamp | float | UTC epoch |
| alert_type | int | `1` = same BSSID different SSID, `2` = same SSID different BSSID + weaker enc |
| rogue_bssid | MAC | BSSID of the suspected rogue AP |
| known_bssid | MAC | BSSID of the known legitimate AP (or `00:00:00:00:00:00` if N/A) |
| ssid_hex | hex | SSID involved |
| rogue_enc | int | Encryption reported by rogue |
| known_enc | int | Encryption of known AP |
| rogue_rssi | int | dBm |
| channel | int | Channel |
| time_flag | int | 0 = synced, 1 = degraded |

**Field count:** 12

### 7.4 `$DF` — Deauth Flood Alert

```
$DF,W24,timestamp,src,target,count,window_s,channel,time_flag*XX\n
```

| Field | Type | Description |
|---|---|---|
| branch_id | string | `W24` |
| timestamp | float | UTC epoch at threshold crossing |
| src | MAC | Source MAC of the flood |
| target | MAC | Destination MAC (or `FF:FF:FF:FF:FF:FF` for broadcast) |
| count | int | Number of deauth/disassoc frames in the window |
| window_s | int | Window duration in seconds (5) |
| channel | int | Channel observed on |
| time_flag | int | 0 = synced, 1 = degraded |

**Field count:** 9

### 7.5 `$BS` — Branch Status

Aggregate health report covering the Branch Controller and all Leaves. Sent every 10 seconds.

```
$BS,W24,uptime_s,time_valid,fix_ok,pps_age_ms,w1_st,w2_st,w3_st,w4_st,q_det_used,q_wids_used,dedup_count,err_count*XX\n
```

| Field | Type | Description |
|---|---|---|
| branch_id | string | `W24` |
| uptime_s | int | RP2040 seconds since boot |
| time_valid | int | 1 if PPS-synced time is valid |
| fix_ok | int | 1 if STM32 GPS has fix (from last $TM) |
| pps_age_ms | int | Milliseconds since last PPS edge |
| w1_st | int | Leaf W1 status: 0=offline, 1=online, 2=degraded |
| w2_st | int | Leaf W2 status |
| w3_st | int | Leaf W3 status |
| w4_st | int | Leaf W4 status |
| q_det_used | int | Detection Queue current occupancy (high water mark indicator) |
| q_wids_used | int | WIDS Queue current occupancy |
| dedup_count | int | Current AP count in dedup table |
| err_count | int | Cumulative: queue overflows + checksum errors + batch mismatches |

**Field count:** 14

---

## 8. Downstream Messages (STM32 → RP2040)

### 8.1 `$TM` — Time Message

Defined in §3.2. Sent by STM32 after each PPS edge.

### 8.2 `$RC` — Relay Command

The STM32 (or Trunk via STM32) can send commands that the RP2040 relays to a specific Leaf. This avoids the STM32 needing to understand Leaf protocol details.

```
$RC,leaf_id,cmd*XX\n
```

| Field | Type | Description |
|---|---|---|
| leaf_id | string | Target Leaf (`W1`–`W4`) |
| cmd | string | Complete Leaf command to relay (e.g., `$CH,W4,1057*5B`) |

The RP2040 extracts the `cmd` field, validates its checksum independently, and forwards it to the appropriate PIO UART. The RP2040 does not interpret the relayed command — it's a transparent passthrough.

This enables the Trunk to retarget the WIDS Leaf's channel mask without the STM32 firmware understanding `$CH`.

### 8.3 `$RQ` — Request Branch Status

```
$RQ,W24*XX\n
```

The RP2040 responds with an immediate `$BS`. Used by the STM32 or Trunk to poll Branch health outside the normal 10-second heartbeat cycle.

---

## 9. Module Decomposition

```
branch_wifi24/
├── CMakeLists.txt              # RP2040 SDK build
├── src/
│   ├── main.c                  # Entry point, core launch, init sequence
│   ├── core0_leaf_io.h/.c      # Core 0: PIO UART management, line assembly, dispatch
│   ├── core1_upstream.h/.c     # Core 1: main loop, dedup flush, WIDS checks, upstream TX
│   ├── pio_uart.h/.c           # PIO UART driver (init, DMA, read/write helpers)
│   ├── proto.h/.c              # Shared: framing, checksum, field parsing, hex encode/decode
│   ├── leaf_health.h/.c        # Leaf state tracking, watchdog, recovery
│   ├── leaf_cmd.h/.c           # Downstream command construction ($CF, $CH, $PG, $RB)
│   ├── pps_time.h/.c           # 1PPS interrupt, $TM parsing, timestamp computation
│   ├── dedup.h/.c              # AP deduplication hash table
│   ├── wids.h/.c               # Evil twin detection, deauth flood tracking
│   ├── queues.h/.c             # Inter-core SPSC ring buffers with spinlock
│   └── upstream_fmt.h/.c       # Upstream message formatting ($WA, $WP, $ET, $DF, $BS)
├── pio/
│   ├── uart_tx.pio             # PIO UART TX program
│   └── uart_rx.pio             # PIO UART RX program
└── include/
    └── branch_defs.h           # Constants, structs, enums
```

**Build system:** RP2040 C SDK with CMake. Not Arduino — the dual-core launch (`multicore_launch_core1()`), PIO programs, DMA channels, and hardware spinlocks all require the C SDK.

---

## 10. State Machine — Branch Controller

### 10.1 Top-Level States

```
[BOOT]
    │
    ├── Init PIO UARTs
    ├── Init UART0 (STM32)
    ├── Init GPIO10 (PPS interrupt)
    ├── Init queues, dedup table, WIDS tables
    ├── Launch Core 1
    │
    ↓
[WAIT_PPS] ────────────────────────────────────────┐
    │                                               │
    │ (PPS received + $TM received → time valid)    │ (10s timeout → proceed without sync)
    ↓                                               ↓
[INIT_LEAVES] ←────────────────────────────────────┘
    │
    ├── Send $CF to W1–W4
    ├── Wait for $HB responses (5s timeout per Leaf, 3 retries)
    │
    ↓
[RUNNING]
    │
    ├── Core 0: Leaf I/O loop (§5.8)
    ├── Core 1: Time + Analysis + Upstream loop (§6.5)
    │
    ↓ (never exits — runs until power off)
```

### 10.2 WAIT_PPS Rationale

The Branch Controller waits up to 10 seconds for valid time before configuring Leaves. This ensures the first detections have PPS-synced timestamps rather than degraded ones. If PPS doesn't arrive (GPS cold start, no fix yet), the controller proceeds anyway — better to capture data with degraded timestamps than miss data entirely. The `time_flag` field on upstream records distinguishes the two cases.

---

## 11. Memory Budget

| Allocation | Size | Notes |
|---|---|---|
| PIO UART RX buffers (4×) | 2 KB | 512 bytes each |
| Line assembly buffers (4×) | 800 B | 200 bytes each |
| Detection Queue | 12 KB | 256 × 48 bytes |
| WIDS Queue | 8 KB | 128 × 64 bytes |
| Dedup table | 26 KB | 512 × 52 bytes |
| Deauth tracker | 1 KB | 64 × 16 bytes |
| Leaf state (4×) | 256 B | ~64 bytes each |
| Time state | 32 B | |
| Stack (Core 0) | 4 KB | |
| Stack (Core 1) | 4 KB | |
| UART0 TX/RX buffers | 1 KB | |
| **Total** | **~56 KB** | Out of 264 KB SRAM |

Headroom: ~208 KB free. No memory pressure.

---

## 12. PIO UART Notes

### 12.1 Program Sharing

All 4 PIO UART RX state machines run the same PIO program loaded once into PIO0 instruction memory. Similarly for TX. PIO0 has 32 instruction words — a basic UART RX program uses ~8 instructions, TX uses ~6. Both fit comfortably.

### 12.2 Baud Rate Configuration

Each SM's clock divider is set independently. For 230400 baud with a 125 MHz system clock:

```
div = 125_000_000 / (230400 * 8) = 67.82
```

Set as `sm_config_set_clkdiv(&c, 67.82f)`. The 8× oversampling is standard for PIO UART.

### 12.3 DMA Configuration

Each RX SM has a dedicated DMA channel configured for:
- Source: PIO RX FIFO for that SM
- Destination: per-Leaf circular byte buffer
- Transfer size: 1 byte
- Count: buffer size (512)
- Wrap: destination address wraps (ring buffer mode)
- Trigger: DREQ from PIO RX FIFO

Core 0 reads from the circular buffer using a software read pointer, comparing against the DMA write pointer to determine available bytes.

---

## 13. Testing Procedure

### 13.1 Unit Tests (No Hardware)

1. **Proto module:** Feed known byte sequences into the checksum function, verify output. Feed valid and corrupted lines into the parser, verify accept/reject.
2. **Dedup module:** Insert records with overlapping BSSIDs at different RSSIs, verify strongest-wins. Verify window expiry and eviction.
3. **WIDS module:** Insert beacon records matching and not matching known APs. Verify evil twin alerts fire correctly. Insert deauth sequences, verify flood threshold.
4. **Queue module:** Producer/consumer correctness with wrap-around. Overflow behavior.

### 13.2 Bench Test — RP2040 + 1 Leaf

1. Wire a single ESP32-C3 to PIO0 SM0
2. Connect RP2040 UART0 TX to a USB-serial adapter (observe upstream output)
3. Simulate PPS by toggling a GPIO pin at 1 Hz from an Arduino or signal generator
4. Send `$TM` messages to UART0 RX from a serial terminal
5. Power on — verify `$BS` appears on upstream with `w1_st=1`, others offline
6. Verify `$WA` messages appear with valid timestamps
7. Disconnect PPS — verify `time_flag` switches to `1` (degraded) on subsequent records

### 13.3 Bench Test — RP2040 + 4 Leaves

1. Full wiring per §1
2. Verify all 4 Leaves come online (`$BS` shows all status `1`)
3. In a multi-AP environment, verify dedup is working: count unique BSSIDs in upstream `$WA` over 10 seconds, compare against raw `$AP` count from individual Leaves (should be fewer upstream)
4. Trigger deauth event → verify `$DF` appears upstream (not raw `$DE`)
5. Kill power to one Leaf → verify `$BS` shows it offline after timeout
6. Restore power → verify automatic re-configuration

### 13.4 Integration Test — RP2040 + STM32

1. Connect RP2040 UART0 to STM32 Branch UART (USART1)
2. STM32 sends `$TM` after each GPS PPS
3. Verify end-to-end: AP detected by Leaf → parsed by RP2040 → deduplicated → timestamped → sent as `$WA` → received by STM32
4. Verify STM32 can send `$RC` to retarget W4 channel mask

---

## 14. Open Items

| # | Item | Status |
|---|------|--------|
| 1 | STM32 firmware: $TM generation after each PPS edge | Not yet specified |
| 2 | STM32 firmware: $RC relay passthrough handling | Not yet specified |
| 3 | STM32 firmware: Branch UART parsing and SD card write format | Not yet specified |
| 4 | 1PPS signal integrity over wiring to multiple RP2040s | Needs verification at assembly |
| 5 | PIO UART program: select from SDK examples or write custom | Implementation decision |
| 6 | DMA ring buffer wrap-around edge case testing | Implementation phase |
| 7 | Final GPIO pin assignments pending PCB layout | Preliminary in §1 |
| 8 | Evil twin detection: threshold tuning for enterprise multi-AP environments | Needs field testing |
| 9 | Dedup window duration: 500 ms baseline, may need tuning per scan rate | Needs field testing |
| 10 | Power regulation: RP2040 + 4× ESP32-C3 total draw and supply requirements | Not yet analyzed |
