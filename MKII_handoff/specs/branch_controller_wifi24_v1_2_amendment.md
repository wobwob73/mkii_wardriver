# Branch Controller (2.4 GHz WiFi) v1.2 — Amendment

**Date:** 2026-06-02
**Status:** Amendment to `branch_controller_wifi24_v1_0.md` + v1.1 amendment (2026-04-04, 2026-05-29 series)
**Scope:** Review-driven correctness fixes from the external code analysis report. Same amendments apply by reference to `wifi5_branch_v1_0.md` since the 5 GHz BC source is a structural copy of the 2.4 GHz one.

This amendment supersedes specific subsections of v1.0/v1.1 listed below. Unmentioned sections remain unchanged.

---

## §8.2 `$RC` Relay — Replace Wire Format (was "plaintext-nested framed inner")

The v1.1-amendment shape `$RC,<target>,<inner_framed_line>*<outer_cksum>` was **unparseable on the wire**: the outer line's checksum-finding loop hits the inner `*XX` first (which is not the outer checksum) and the comma split on the body shreds the inner command's own fields. v1.0/v1.1 firmware that attempted to consume that shape relayed nothing.

**New wire format:**

```
$RC,<target_leaf_id>,<hex>*<outer_cksum>\n
```

where `<hex>` is the ASCII-hex of the entire inner framed line **including** its leading `$`, body, `*`, and its two inner checksum digits — but **excluding** any trailing newline. Hex encoding eliminates all `*` and `,` collisions with the outer framing (mirroring the existing SSID hex convention) and removes any ambiguity about where the outer line ends.

### Example

Relayed inner command (e.g. set WIDS channel mask on W4):
```
$CH,W4,1057*5B
```
hex-encoded:
```
2443482C57342C313035372A3542
```
Wrapped in `$RC` to W4:
```
$RC,W4,2443482C57342C313035372A3542*<outer>\n
```
where `<outer>` is the XOR checksum of every byte between the leading `$` and the closing `*` of the outer line.

### BC parse procedure

After the outer line is received and validated by the line receiver:

1. Strict outer-line checksum validation (see §8.3 below — `*XX` must end the line).
2. Split body by `,`, expect exactly three fields: `RC`, `<target>`, `<hex>`.
3. Hex-decode `<hex>` into a byte buffer (≤ `MAX_LINE_LEN`).
4. Treat the decoded buffer as a framed inner line; validate its checksum with the same validator used for outer lines.
5. Look up `<target>` in the leaf-id table; if unknown, drop and increment `relay_unknown_leaf`.
6. Append `\n` to the decoded inner and send via the leaf's PIO TX pin.

### STM32 side

The STM32 forwards the outer `$RC,...*XX\n` line **verbatim** to the target Branch UART after validating the outer checksum (§8.3). The STM32 does not hex-decode; the BC does. This keeps the STM32 a layer-3-style router on `leaf_id`.

### Why not the alternative

The "find the *last* `*` in the outer line" form was considered. It works on well-formed input but stays brittle: any inner command that happens to contain a literal `*` (none defined today, but no prohibition either) re-introduces the ambiguity. Hex encoding is a one-time cost — ~2× the line length on the BC↔Trunk hop only — and removes the class of failures entirely.

---

## §8.3 Outer Line Checksum — Strict Trailing-Data Rejection

`proto_validate_line()` previously accepted any line whose `*XX` checksum-digit pair fit anywhere within the buffer (`star + 3 ≤ len`). With the v1.2 `$RC` hex form this is fine, but it also let arbitrary trailing bytes after a valid `*XX` slip through unflagged — a framing hole that could hide line concatenation or injection bugs.

**Replace** the size check with strict end-of-line placement:

```
require: star + 3 == line_end
```

The line receiver MUST strip a trailing `\r` (CRLF terminators) before zero-terminating, so the validator sees `*XX` as the final three bytes regardless of how the upstream link framed the newline. This applies to **every** proto consumer (leaf, BC, env_sensor, STM32).

---

## §10 PIO TX Pin Retargeting — Replace Implementation

v1.1 said the BC "remaps the PIO TX OUT pin between consecutive sends" using a one-SM-per-branch design. The v1.0/v1.1 source achieved this by re-calling `pio_gpio_init()` for the new pin but never updating the state machine's `PINCTRL.OUT_BASE` / `PINCTRL.SIDESET_BASE` — the registers that actually choose which pin the program writes. Every downstream byte therefore left on the originally-configured pin (W1 on 2.4 GHz, W5_1 on 5 GHz), and W2/W3/W4 (and W5_2/W5_3) never received `$CF`, `$CH`, `$PG`, `$RB`, or relayed commands. W4 WIDS mode in particular was unreachable.

**New procedure** for `uart_tx_retarget_pin()` (implementation in `src/pio_uart.c`):

1. If the requested pin already matches the current SM target, return immediately.
2. Wait for the TX FIFO to drain, then wait ≥ one character time (80 µs at LEAF_BAUD = 230 400) so the in-flight character finishes on the previous pin.
3. Disable the SM.
4. Release the previous pin: `gpio_set_function(prev_pin, GPIO_FUNC_SIO)`, `gpio_set_dir(prev_pin, GPIO_IN)` so it stops driving.
5. Claim the new pin for PIO: `pio_sm_set_pins_with_mask(...)`, `pio_sm_set_pindirs_with_mask(...)`, `pio_gpio_init(...)`.
6. **Rebuild** a full `pio_sm_config` with `sm_config_set_out_pins(&c, new_pin, 1)` and `sm_config_set_sideset_pins(&c, new_pin)`; apply via `pio_sm_set_config(pio, sm, &c)`.
7. Clear FIFOs and `pio_sm_exec(...)` a `jmp` to the program origin so the SM resumes from a known state.
8. Re-enable the SM.
9. **Self-check**: read back `pio->sm[sm].pinctrl` and assert `OUT_BASE == new_pin` and `SIDESET_BASE == new_pin`. If not, increment `pio_uart_tx_retarget_error_count()` and drop the send.

The one-SM design is retained — it scales to the 8-leaf UHF ISM branch in a way that one-SM-per-leaf does not.

The retarget error count MUST be surfaced in the `$BS` heartbeat alongside the existing `err_count` (or added as a dedicated field in a future amendment).

---

## §11 PPS / Time State — Replace Concurrency Model

The PPS GPIO ISR runs on core0; readers and the `$TM`-apply path run on core1. v1.0/v1.1 accessed the state struct's fields directly, with only `pps_timer_us` / `pps_count` / `pps_pending` marked `volatile` and no inter-core synchronization. This allowed:

- a 64-bit `pps_timer_us` to tear across the inter-core read,
- `apply_tm` on core1 to interleave with an ISR fire on core0,
- the reader to observe `time_valid = true` against a stale `epoch_s`.

**New pattern:**

- A single SDK hardware spinlock (`spin_lock_claim_unused()` / `spin_lock_init()`) protects every read and every write of the state struct.
- All writers (ISR, `pps_time_apply_tm()`, `pps_time_health_check()`) take the lock with `spin_lock_blocking()`, mutate, then `spin_unlock()`.
- All readers take the lock, copy fields into a `pps_snapshot_t`, then unlock and operate on the snapshot.
- The state struct is no longer exposed in the public header; the snapshot type (`pps_snapshot_t`) is the contract.

Snapshot fields (additions / clarifications):

```c
typedef struct {
    uint64_t pps_timer_us;
    uint32_t epoch_s;
    uint32_t pps_count;
    bool     time_valid;
    bool     fix_ok;
    bool     pps_pending;
} pps_snapshot_t;
```

`pps_time_snapshot(out)` returns `true` after populating `*out`. The same pattern (with `__disable_irq()`/PRIMASK restore in place of the spinlock) applies on the single-core STM32 aggregator and is described in `stm32_h753_firmware_v1_0.md` §6 (updated).

---

## §12 WIDS Evil-Twin — Partial Correctness Fix (full redesign deferred)

The v1.1 detector's "same BSSID, different SSID" path populated **both** `rogue_bssid` and `known_bssid` with `bc->bssid`, so the alert could not identify two radios. The detector also never emitted `ET_ENC_MISMATCH`.

**Changes:**

- `ET_SSID_MISMATCH` and `ET_ENC_MISMATCH` alerts leave `known_bssid` zeroed when the detector has only a single BSSID's history to compare against (which is always the case with v1.0 BSSID-keyed dedup). The analyzer can distinguish the single-radio case (zero `known_bssid`) from the future two-radio case once SSID-keyed tracking lands.
- A new `ET_ENC_MISMATCH` alert is emitted when the dedup record's encryption differs from the current beacon's encryption (with SSIDs matching).

**Deferred to a future amendment:** the full SSID-keyed evil-twin detector (same SSID across different BSSIDs, security downgrade, channel/RSSI anomalies). v1.2 fixes only the existing detector's correctness.

---

## §13 Leaf UART Transmit — Bound the Send Loop

The leaf `uart_proto::send_line()` previously spun until `Serial.availableForWrite() >= n`. If the hardware TX buffer were ever smaller than `MAX_LINE_LEN` (200) under backpressure, the leaf would lock up forever. The Arduino-ESP32 v3.x core's UART TX buffer is large enough that this hasn't been observed, but the code shape is the bug.

**Replace** with chunked writes: write only as much as `availableForWrite()` reports, yield via `delay(0)`, and abort after a 50 ms deadline. Aborts increment `tx_drop_count()`, surfaced in `$HB`.

---

## Cross-References

- `branch_controller_wifi24_v1_0.md` — base spec.
- `branch_controller_wifi24_v1_1_amendment.md` — supersedes §4.1 / §8.2 / §9 / §11 (the v1.1 §8.2 "$RC inner-checksum-on-relay" requirement is RETAINED — the inner is still independently checksum-validated after hex-decode).
- `wifi5_branch_v1_0.md` — same amendments apply to the 5 GHz BC.
- `stm32_h753_firmware_v1_0.md` §7 — STM32-side `$RC` routing (now requires strict outer validation and is opaque to the hex inner).
- External code analysis report `mkii_code_analysis_report.md` (2026-06-01) — F-001, F-002, F-004, F-006, F-007, F-008.
