# 2.4 GHz WiFi Branch — Leaf Protocol v1.3 Amendment

**Version:** 1.3.0
**Date:** 2026-06-02
**Status:** Amendment to `wifi24_leaf_protocol_v1_1.md` (2026-04-04), as amended by `wifi24_leaf_protocol_v1_2_amendment.md` (2026-05-29)
**Changes:** Adds a third Leaf type — **Scan-Hop** (`mode=2`) — a single passive scan Leaf that round-robins a channel set instead of parking on one channel. `$CF` gains `mode=2`; `$CH` is extended to apply to Scan-Hop as well as WIDS; a new firmware-architecture section (§6A) specifies the Scan-Hop Leaf; defaults, standalone fallback, and the version macro are updated. Introduced for the light-duty single-box variant (`lite_aggregator_v1_0.md`), where one Leaf must cover 2.4 GHz alone.

This document specifies the exact changes to v1.1.0 + v1.2.0. Section numbers refer to v1.1.0 unless noted. Anything not mentioned here is unchanged. Scan-Hop is additive — it does not alter Scan (`mode=0`) or WIDS (`mode=1`) behavior, and the single-binary model is preserved (a Leaf becomes Scan-Hop only on receipt of `$CF` with `mode=2`).

---

## Rationale

The full Branch covers 2.4 GHz with three parked Scan Leaves (W1/W2/W3 on channels 1/6/11) running continuously and concurrently. A light-duty unit has one 2.4 GHz Leaf and cannot park three channels at once. Scan-Hop time-slices a channel set on a single Leaf: it runs a passive scan on each channel in the set in turn, advancing after each. It trades the concurrency of the three-Leaf array (continuous coverage of all three centers) for a single Leaf that sweeps the set on a duty cycle. Default coverage follows the project's established 1/6/11 model (`system_plan_v2.md` 2.4 GHz channel physics): channels 1/6/11 capture the 11 US channels via passband overlap.

Scan-Hop emits the **same records as Scan** (`$AP`, `$BK`, `$HB`) — there are no new message types. A Branch Controller (or the lite aggregator) ingests Scan-Hop exactly as it ingests Scan; only the `leaf_id` and the per-sweep `$BK` cadence differ.

---

## New Leaf-ID Convention — Scan-Hop

Scan, WIDS, and Scan-Hop are three roles of one firmware binary; `leaf_id` is a Branch-Controller-assigned string and carries the role by convention:

| Role | `mode` | `leaf_id` convention |
|---|---|---|
| Scan (parked) | 0 | `W1`–`W3` |
| WIDS (promiscuous hop) | 1 | `W4` |
| **Scan-Hop (passive sweep)** | **2** | **`WH1`** (additional Leaves `WH2`, … if ever fielded) |

`WH1` parallels the `W5_1` / `BLE-1` suffix convention and is disjoint from the `W24` **branch_id**, avoiding the `$WA,W24,…,leaf_id=…` collision that a `W24` leaf_id would create.

---

## §3.1 `$AP` — Receiver Bound (leaf_id)

No field or format change. The `leaf_id` range in the field table is widened from `W1`–`W4` to include `WH1` (and `WH2`, … if fielded). A Scan-Hop Leaf populates the `channel` field with the channel it was scanning when the AP was heard.

## §3.2 `$BK` — Batch-End Semantics for Scan-Hop

No field or format change. For Scan-Hop, one `$BK` is emitted **per full sweep of the channel set** (not per single-channel scan): `count` = total `$AP` across the sweep, `scan_ms` = total sweep duration including inter-channel retune. This keeps `$BK` meaning "one scan cycle" — for Scan-Hop a cycle is one pass over the hop set. The Branch Controller's received-`$AP`-vs-`$BK.count` reconciliation (v1.1 §3.2) is unchanged.

---

## §4.1 `$CF` — Add `mode=2` (Scan-Hop)

The `$CF` wire format is unchanged:

```
$CF,leaf_id,mode,channel,param1,param2*XX\n
```

The `mode` field and the per-mode meaning of `channel` / `param1` are extended:

| Field | Scan (`mode=0`) | WIDS (`mode=1`) | **Scan-Hop (`mode=2`)** |
|---|---|---|---|
| mode | 0 | 1 | **2** |
| channel | park channel (1–14) | `0` (hop-all) | **`0`** — hop set comes from the channel mask (default 1057 = 1/6/11); a non-zero value is ignored |
| param1 | `0` (unused) | dwell ms/ch (default 100) | **passive dwell ms/ch (default 200)** — the per-channel passive-scan time; see §6A |
| param2 | `0` reserved | `0` reserved | `0` reserved |

**Field count:** 6 (unchanged).

Example (light-duty 2.4 GHz Leaf, 1/6/11, 200 ms/ch):
```
$CF,WH1,2,0,200,0*5B
```

A `$CF` with `mode=2` resets the channel mask to the Scan-Hop default (1057 = 1/6/11); a subsequent `$CH` overrides it (§4.2). The Leaf responds with `$HB` after applying the configuration, as for all modes. The receiver validation bound on `mode` widens from `{0,1}` to `{0,1,2}`.

## §4.2 `$CH` — Now Applies to WIDS **and** Scan-Hop

Retitle from "Channel Mask (WIDS only)" to "Channel Mask (WIDS and Scan-Hop)". Format unchanged:

```
$CH,leaf_id,mask*XX\n
```

`mask` is the same 14-bit bitmask (bit 0 = channel 1 … bit 13 = channel 14). Application rules:

- **WIDS (`mode=1`)** — unchanged from v1.1 §4.2.
- **Scan-Hop (`mode=2`)** — the mask selects the channels the Leaf sweeps. Default after `$CF,…,2,…` is **1057** (1/6/11). A Leaf in Scan-Hop applies a received `$CH` and responds with `$HB`. `mask=0` is invalid and ignored (Leaf keeps current set, `$HB` unchanged), as for WIDS.
- **Scan (`mode=0`)** — still ignores `$CH` (a parked Leaf has a single channel); responds with `$HB` unchanged.

Worth noting Scan-Hop typically uses 1057 (1/6/11) or 2047 (all of 1–11). Including channels 12–14 in a Scan-Hop mask is permitted but off by default (those are the unauthorized-US channels WIDS sweeps for rogue detection, not normal collection).

---

## §6A. Firmware Architecture — Scan-Hop Leaf (NEW)

Inserted after §6 (Scan Leaf). Scan-Hop reuses the Scan Leaf's modules (§6.2) unchanged; only the scan driver advances a channel cursor across the hop set and the `$BK` is emitted per sweep.

### 6A.1 State Machine

```
[BOOT] ──(UART ready)──→ [WAIT_CONFIG]
                              │
              ($CF mode=2)    │  (10s timeout → §5.2 Scan-Hop fallback)
                    ↓         ↓
              [CONFIGURE] (set hop mask = default 1057 or $CH override; dwell = param1)
                    │
                    ↓
              [SWEEP] ←────────────────────────────┐
                    │                                │
        (passive scan ch = hop_set[cursor], dwell)   │
        (on complete: send $AP × N for this channel)  │
                    │                                │
        (advance cursor; wrap → end of sweep:         │
            send $BK[total_count, sweep_ms];          │
            reset totals)                             │
                    │                                │
              [IDLE_CHECK]                            │
        (heartbeat due? send $HB)                     │
        (command pending? process $CF/$CH/$PG/$RB)    │
                    └────────────────────────────────┘
```

### 6A.2 Main Loop — Scan-Hop Mode

```
setup():
    init UART0 at 230400 (Branch Controller link)
    init WiFi in station mode (no connect)
    send $HB (boot announce)
    start 10s config timeout

configure(mode=2):
    hop_set  = channels_from_mask(mask)      // default mask 1057 → {1,6,11}
    dwell_ms = param1 ?: 200
    cursor   = 0
    sweep_count = 0; sweep_start = now

loop():
    check_incoming_commands()                // non-blocking; $CH may re-derive hop_set live

    if state == SWEEP and scan_not_in_progress:
        start_async_passive_scan(hop_set[cursor], dwell_ms)

    if scan_complete:
        for each AP in results:              // channel field = hop_set[cursor]
            send_ap_message(AP)
        sweep_count += ap_count
        WiFi.scanDelete()
        cursor = (cursor + 1) % len(hop_set)
        if cursor == 0:                       // wrapped → sweep finished
            send_bk_message(sweep_count, now - sweep_start)
            sweep_count = 0; sweep_start = now

    if heartbeat_due():
        send_heartbeat()
```

### 6A.3 Implementation Notes — Scan-Hop Leaf

- **Async passive scan, per channel.** `WiFi.scanNetworks(/*async=*/true, /*show_hidden=*/true, /*passive=*/true, /*max_ms_per_chan=*/dwell_ms, /*channel=*/hop_set[cursor])`. Async is mandatory (§6.4): a blocking scan stalls the UART RX path and drops commands. The passive flag keeps the Leaf listen-only (`wifi24_leaf_protocol_v1_2_amendment.md` §6.4); Scan-Hop never sends probe requests.
- **Dwell = per-channel passive time.** `dwell_ms` (param1, default 200) is the listen window per channel — long enough to catch a beacon interval (typ. 102.4 ms). 200 ms/ch over 1/6/11 is a ~0.6 s sweep plus retune; over all of 1–11 (mask 2047) it is ~2.2 s. Retune between channels is sub-millisecond and not the limiter (consistent with the payload-doc finding that channel count, not retune, dominates).
- **`$BK` per sweep, not per channel.** Emit `$AP` as each channel's results arrive (lowest latency to the BC), but accumulate `count` and emit a single `$BK` when the cursor wraps. The BC's per-sweep reconciliation then matches.
- **Result lifetime.** `WiFi.scanDelete()` after each channel's iteration and before the next channel's scan (§6.4) — the cursor advance does not exempt this.
- **Live `$CH` re-derivation.** If a `$CH` arrives mid-sweep, finish the current channel, then rebuild `hop_set` from the new mask and reset the cursor to 0 (start a fresh sweep). Do not mutate `hop_set` underneath an in-flight scan.
- **No promiscuous mode.** Scan-Hop is a Scan-class role (channel-by-channel `scanNetworks`), not WIDS. It does not see `$DE`/`$PR`/`$BC` traffic and emits none. WIDS detection in a single-Leaf 2.4 GHz unit is out of scope (`lite_aggregator_v1_0.md` §6.1, §14 item 2).

---

## §5.2 Leaf Standalone Fallback — Replace

**Replaces** the v1.1 §5.2 default. A Leaf that receives no `$CF` within 10 s now enters **Scan-Hop with the default mask 1057 (1/6/11) and 200 ms dwell**, under `leaf_id = WH?`, and begins emitting `$AP`/`$BK`/`$HB`. The previous fallback (Scan, channel 1) is retired as the default.

This is a firmware-wide change to the single binary, not a per-role one: it affects the no-`$CF` behavior of every unit, including W1–W4 in the full Branch. The impact is confined to bench/missed-config scenarios — in the full system the Branch Controller always sends an explicit `$CF` at boot (v1.1 §5.1), which overrides the fallback before any scanning begins, so a fielded W1/W2/W3 still parks and W4 still runs WIDS exactly as before. The change only improves the controller-less bench case (one Leaf on a USB-serial adapter now sweeps 1/6/11 instead of sitting on ch 1).

---

## §9 Configuration Defaults — Add Scan-Hop Rows

| Parameter | Default |
|---|---|
| mode | **2 (scan-hop) — firmware-wide fallback** |
| channel | `0` (hop set from mask; the standalone ch-1 park default is retired) |
| dwell_time_ms | 100 (WIDS), **200 (Scan-Hop)** |
| channel_mask | **1057 (1/6/11) — default for the Scan-Hop fallback**; 16383 (all 1–14) only when WIDS is selected by `$CF` |
| heartbeat_interval_s | 10 |

Defaults remain compiled-in, not flash-stored; every boot starts from defaults, then `$CF` overrides.

---

## §10 Build Configuration — Update Version Macro

Change `-DLEAF_VERSION=\"1.2.0\"` to `-DLEAF_VERSION=\"1.3.0\"` in `platformio.ini`. The fallback default in `leaf_defs.h` follows the same change. No other build flags change. (`CODE_STATUS.md` should be reconciled: the `leaf_wifi24` row currently shows `1.2.1`; after this amendment the firmware version is `1.3.0`, and the version string must read `1.3.0` in `platformio.ini`, `leaf_defs.h`, and this §10 example — one string, three places.)

The Scan-Hop code path is gated by no compile flag; it is always present in the single binary and selected at runtime by `$CF mode=2`. The lite aggregator's `AGG_W24_SCANHOP` flag (`lite_aggregator_v1_0.md` §12.2) governs only whether the *aggregator* sends `mode=2`, not the Leaf build.

---

## §13 Open Items

| # | Item | Status |
|---|------|--------|
| A | Scan-Hop dwell tuning: 200 ms/ch baseline vs beacon-interval coverage in dense environments | Needs field testing |
| B | Default hop set 1057 (1/6/11) overlap-coverage assumption vs explicit all-channel sweep (2047) for the single-Leaf case | Config choice; documented |
| C | 2.4 GHz WIDS without a dedicated promiscuous Leaf | Out of scope; see `lite_aggregator_v1_0.md` §14 item 2 |

---

## Summary of All Changes

| Section | Change Type | Description |
|---------|-------------|-------------|
| §3.1 | Widen bound | `leaf_id` range includes `WH1`; Scan-Hop sets `channel` to the channel being scanned |
| §3.2 | Clarify | Scan-Hop emits one `$BK` per full sweep (count/duration aggregated over the hop set) |
| §4.1 | Extend | Add `mode=2` (Scan-Hop); per-mode meaning of `channel`/`param1` tabulated; `mode` bound → `{0,1,2}` |
| §4.2 | Extend scope | `$CH` now applies to Scan-Hop as well as WIDS; Scan-Hop default mask 1057 |
| §6A | New section | Scan-Hop Leaf state machine, main loop, implementation notes |
| §5.2 | Replace default | Standalone fallback is now Scan-Hop (mask 1057, 200 ms), firmware-wide; replaces Scan/ch1 |
| §9 | Change defaults | Fallback mode 0→2; default mask 1057, dwell 200; ch-1 park default retired |
| §10 | Update macro | `LEAF_VERSION` → 1.3.0; `CODE_STATUS.md` reconciliation note |
| §13 | New items | Scan-Hop open items A–C |

---

## Cross-Reference Resolution

This amendment is the Leaf-side prerequisite called out in `lite_aggregator_v1_0.md` §15 item 1. After it is incorporated:
- `lite_aggregator_v1_0.md` §6 (W24 slot): `leaf_id` is **`WH1`** (not `W24`); mode is Scan-Hop (`mode=2`).
- `lite_aggregator_v1_0.md` §6.4 boot `$CF`: `$CF,WH1,2,0,200,0*XX`, optionally followed by `$CH,WH1,1057*XX` (default set; explicit send only if overriding).
- `lite_aggregator_v1_0.md` §7 `$WA` for the 2.4 GHz slot: `branch_id=W24`, `leaf_id=WH1`.

The document version ladder for the WiFi 2.4 GHz Leaf protocol is now:
`wifi24_leaf_protocol_v1_1.md` (2026-04-04) → `wifi24_leaf_protocol_v1_2_amendment.md` (2026-05-29) → `wifi24_leaf_protocol_v1_3_amendment.md` (2026-06-02). Read all three together; each amendment restates only the sections it changes.
