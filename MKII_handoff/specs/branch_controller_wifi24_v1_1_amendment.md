# 2.4 GHz WiFi Branch — RP2040 BC Firmware Guide v1.1 Amendment

**Version:** 1.1.0
**Date:** 2026-05-29
**Status:** Amendment to `branch_controller_wifi24_v1_0.md` (2026-04-04)
**Changes:** Inter-core queues codified as true SPSC with `__dmb()` barriers (no spinlocks). PIO usage codified as PIO0 = 4× RX, PIO1 = 1× TX with pin remap. Dedup eviction changed from rehash-on-eviction to tombstone-based reclaim (fixes the double-increment bug). `$RC` relay checksum-on-forward documented. `enc` field range widened to 0–10 to match Leaf v1.2.

This document specifies the exact changes to v1.0.0. Section numbers refer to v1.0.0 unless noted. Anything not mentioned is unchanged. The intent is to bring the spec into line with the implementation choices that were already in the (re-derived) BC firmware and recorded as deviations in `CODE_STATUS.md`.

---

## §4.1 Shared Queues — Replace Description

Replace the v1.0.0 paragraph:

> Two lock-free SPSC (single-producer, single-consumer) ring buffers, protected by RP2040 hardware spinlocks for the rare case of simultaneous access to head/tail pointers.

with:

> Two **true SPSC** (single-producer, single-consumer) ring buffers between Core 0 (producer) and Core 1 (consumer). No spinlocks. The producer owns the write index (`w_idx`); the consumer owns the read index (`r_idx`). 32-bit aligned loads and stores are atomic on the Cortex-M0+ — neither index can be torn. ARM `__dmb()` data memory barriers are placed between the element store and the index update on the producer side, and between the index load and the element load on the consumer side, to enforce ordering across cores.
>
> The v1.0.0 wording "lock-free SPSC… protected by hardware spinlocks" was self-contradictory: a true SPSC ring needs no spinlock, and a spinlock-protected ring is not lock-free. The implementation uses true SPSC. Either index touched by both cores would re-introduce the need for synchronization; the design strictly maintains single-writer ownership for each index, which is the property that makes the spinlock unnecessary.

The table of queues and capacities (Detection Queue 256 × 48 B, WIDS Queue 128 × 64 B) is unchanged.

---

## §4 — Add §4.5 SPSC Implementation Pattern (New Subsection)

```c
// Producer (Core 0):
//   1. Load r_idx (acquire load; we'll compare against this snapshot)
//   2. If (w_idx + 1) mod CAP == r_idx_snapshot -> full, drop, increment overflow counter
//   3. Store element at slot[w_idx]
//   4. __dmb()                      // ensure element is visible before index update
//   5. w_idx = (w_idx + 1) mod CAP  // publish

// Consumer (Core 1):
//   1. Load w_idx (acquire)
//   2. If w_idx == r_idx -> empty, return
//   3. __dmb()                      // ensure index load completes before element load
//   4. Read element at slot[r_idx]
//   5. r_idx = (r_idx + 1) mod CAP

// Invariants:
//   - Only Core 0 writes w_idx; only Core 1 writes r_idx.
//   - Both indices are aligned uint32_t (single-cycle atomic load/store on M0+).
//   - Capacity is a power of two so the modulo collapses to a mask.
//   - No spinlock, no critical section, no interrupt masking.
```

The same pattern applies to both Detection Queue and WIDS Queue. The pattern is also used for any future SPSC queue added to the BC.

**Overflow semantics:** unchanged from v1.0.0 §4.4. The producer increments an overflow counter and proceeds; the counter is exposed in `$BS` `err_count`.

---

## §6.3 AP Deduplication — Replace Eviction Logic

The v1.0.0 dedup table layout (`DedupEntry`, 512 slots, FNV-1a, open-addressing with linear probing) is unchanged. The **eviction strategy** is replaced.

### 6.3.1 v1.0.0 strategy (deprecated) — rehash-on-eviction

v1.0.0 §6.3 implied that stale entries (not seen in the previous window) were evicted by clearing the slot, after which subsequent inserts would naturally rehash through them. This approach contains a double-increment bug: when an evicted slot is cleared but a probe sequence past that slot referenced it as occupied, lookups for keys that originally probed past the evicted slot can be either (a) restarted (causing duplicate insertion when the key is re-encountered) or (b) terminated short (missing an existing entry placed further down the probe chain). Either choice corrupts the dedup table over time.

### 6.3.2 v1.1 strategy — tombstone-based reclaim

Each slot has a 3-state status:

```c
enum DedupSlotState {
    SLOT_EMPTY     = 0,   // never used, terminates probe chains
    SLOT_OCCUPIED  = 1,   // valid entry
    SLOT_TOMBSTONE = 2,   // was occupied, evicted, probe chain continues through
};
```

Stored alongside (or packed into) the existing `DedupEntry`. With 512 slots a 1-byte status field adds 512 B — within budget.

**Insert:**
1. Probe from `hash(bssid) % CAP` linearly.
2. Skip `SLOT_OCCUPIED` slots whose `bssid` doesn't match.
3. **Record the first `SLOT_TOMBSTONE` encountered** as `insert_slot`; continue probing for a match.
4. If the matching `bssid` is found in a later `SLOT_OCCUPIED` slot, update it in place.
5. Otherwise, on reaching a `SLOT_EMPTY`, insert at `insert_slot` (or at this `SLOT_EMPTY` if no tombstone was seen). State becomes `SLOT_OCCUPIED`.

**Lookup:** identical to insert except step 5 returns "not found"; tombstones are walked through, not stopped at.

**Eviction (per window):**
1. At end of each 500 ms dedup window, scan the table.
2. Entries marked `emitted` and whose `local_timer_us` predates the previous window boundary: set state to `SLOT_TOMBSTONE`, leave the byte fields in place (they'll be overwritten on the next insert into that slot).
3. Entries with `emitted == false` (new this window): format as `$WA` upstream, then set `emitted = true` and leave state `SLOT_OCCUPIED`.

**Tombstone compaction:** if the tombstone-to-occupied ratio exceeds 1:1 (more than 256 tombstones in a 512-slot table), a single linear sweep collapses tombstones by re-inserting every `SLOT_OCCUPIED` entry into a fresh table image and swapping pointers. This is bounded and runs at most once per window; in practice it rarely fires because tombstones are reclaimed by subsequent inserts.

Memory budget delta vs v1.0.0: +512 B (status bytes). Dedup table total ~26.5 KB.

### 6.3.3 Correctness sketch

The classic invariant of open-addressing-with-tombstones holds: every probe chain that ever reached an occupied slot still reaches it after eviction (because tombstones don't terminate probes). The double-increment bug from §6.3.1 is impossible because no two probe chains share a "boundary slot" whose state can flip the chain's interpretation.

---

## §8.2 `$RC` Relay — Add Checksum-on-Forward Requirement

Append to v1.0.0 §8.2:

> **Checksum validation on the inner command.** The `cmd` field is itself a complete Leaf command including its own `*XX` checksum and terminating `\n`. After extracting `cmd` from the outer `$RC` line, the RP2040 MUST independently re-compute the XOR checksum of the bytes between the inner `$` and the inner `*` and compare against the inner `XX`. If the inner checksum is invalid, the RP2040 drops the relay (does not forward to the Leaf), increments the BC's `err_count`, and ignores the inner command silently.
>
> Implementation note: the outer line's checksum is consumed by the line-receiver as part of accepting the `$RC` line; the inner checksum cannot reuse that work and needs its own helper. The same helper used for outbound `$CF`/`$CH`/`$PG`/`$RB` generation can be inverted for verification.
>
> The RP2040 still does not interpret the relayed command's *payload* — it remains a transparent passthrough beyond the checksum check.

---

## §9 Module Decomposition — Update Comments

Two comments need updating to match the actual implementation:

| Module | v1.0.0 comment | v1.1 comment |
|---|---|---|
| `queues.h/.c` | "Inter-core SPSC ring buffers with spinlock" | "Inter-core SPSC ring buffers (true SPSC, `__dmb()` barriers)" |
| `pio/uart_tx.pio` | "PIO UART TX program" | "PIO UART TX program (loaded into PIO1; see §12)" |
| `pio/uart_rx.pio` | "PIO UART RX program" | "PIO UART RX program (loaded into PIO0; see §12)" |

No file additions or removals. No build-system changes.

---

## §12.1 PIO Program Layout — Replace Subsection

Replace the v1.0.0 paragraph:

> All 4 PIO UART RX state machines run the same PIO program loaded once into PIO0 instruction memory. Similarly for TX. PIO0 has 32 instruction words — a basic UART RX program uses ~8 instructions, TX uses ~6. Both fit comfortably.

with:

> The RX and TX PIO programs are loaded into **separate PIO blocks** to remove instruction-memory and FIFO contention:
>
> | Block | Program | State machines | Pins |
> |---|---|---|---|
> | **PIO0** | `uart_rx.pio` (loaded once) | SM0 = W1 RX, SM1 = W2 RX, SM2 = W3 RX, SM3 = W4 RX | GP1, GP3, GP5, GP7 |
> | **PIO1** | `uart_tx.pio` (loaded once) | SM0 only | OUT pin remapped per transmission to GP0 / GP2 / GP4 / GP6 |
>
> **Why not co-locate TX in PIO0:** the four RX state machines saturate PIO0 — each RX SM holds its own FIFO and the PIO peripheral has finite FIFO depth across SMs. Adding a TX SM in the same block introduced FIFO contention that occasionally caused the RX path to drop bytes under sustained traffic, even though both programs individually fit in PIO0's 32-word instruction memory.
>
> **Single TX SM with pin remap:** downstream traffic to Leaves (`$CF`, `$CH`, `$PG`, `$RB`) is rare (boot config + occasional channel-mask updates + ping retries). One TX SM is sufficient; the SDK call `sm_config_set_out_pins(&c, target_gp, 1)` is invoked before each transmission to retarget the SM to the correct Leaf's RX pin. The TX SM remains loaded; only the pin binding changes.
>
> Alternative implementation note: four TX SMs in PIO1, one per Leaf, is also acceptable and simplifies the per-transmission code at the cost of three additional SM slots. The v1.1 canonical choice is single-SM-with-remap because PIO1 SM slots are not free if a future Branch (e.g., adds a 5th Leaf or a sensor pin under PIO control) needs them.

---

## §6.3 + §7.1 — Widen `enc` Field Range to 0–10

The `enc` field in the dedup table (`DedupEntry.enc`) and the upstream `$WA` field accept the full 0–10 range as defined in Leaf protocol v1.2 §3.1.1 (adds `LE_OWE = 9` and `LE_WPA3_ENT = 10`). The byte storage is unchanged (`uint8_t`); only the receive-side bounds check in `proto.c` widens.

Implementations that copied a hard-coded `if (enc > 8) reject;` check from v1.0.0-era code need to widen the check to `if (enc > 10) reject;` to avoid silently rejecting OWE and WPA3-Enterprise records. The same widening applies to the WIDS `$BC` parse path and the upstream `$ET` `rogue_enc` and `known_enc` fields in §7.3.

---

## §14 Open Items — Reduced

The following items are addressed by this amendment and may be marked **resolved** in the next consolidated rewrite:

| # | v1.0.0 status | v1.1 status |
|---|---|---|
| 5 | PIO UART program: select from SDK examples or write custom | Resolved — PIO0 RX (loaded once for 4 SMs), PIO1 TX (loaded once, single SM, OUT pin remap per transmission) |

Items 1, 2, 3, 4, 6, 7, 8, 9, 10 from v1.0.0 §14 remain open.

Item 1 (STM32 `$TM` generation) and item 2 (STM32 `$RC` relay) move to the upcoming STM32 firmware guide (roadmap #4). Items 4, 7, 8, 9, 10 are hardware/bring-up tasks unblocked by this amendment.

---

## Summary of All Changes

| Section | Change Type | Description |
|---------|-------------|-------------|
| §4.1 | Replace paragraph | True SPSC with `__dmb()`; no spinlocks |
| §4 | Add §4.5 | Explicit SPSC pattern (producer/consumer ownership + barrier placement) |
| §6.3 | Replace eviction logic | Tombstone-based reclaim; defines slot states, insert/lookup/eviction algorithms; documents double-increment bug avoidance |
| §6.3 + §7.1 + §7.3 | Widen `enc` range | 0–10 (was 0–8) to track Leaf v1.2 §3.1.1 |
| §8.2 | Append requirement | `$RC` inner-command checksum must be re-verified before forward |
| §9 | Update comments | `queues.{h,c}`, `pio/uart_rx.pio`, `pio/uart_tx.pio` |
| §12.1 | Replace subsection | PIO0 = RX×4, PIO1 = TX×1 with pin remap; rationale + alternative |
| §14 | Resolve item | #5 (PIO program selection) resolved |

---

## Cross-Reference Resolution

This amendment resolves all four "Deviations / design calls baked in" listed under `branch_wifi24` in `CODE_STATUS.md` §2. After v1.1 is incorporated, those deviations are spec-conformant behavior.

The document version ladder for the WiFi 2.4 GHz BC firmware guide is now:
`branch_controller_wifi24_v1_0.md` (2026-04-04) → `branch_controller_wifi24_v1_1_amendment.md` (2026-05-29). Read both together.

### Residual stale doc thread (not in scope for this amendment)

The `wifi24_leaf_protocol_v1_1.md` §1 topology diagram still shows a `BN-220 GPS` on the Branch Controller's I2C bus. The v2.1 system-plan amendment moved timing to 1PPS fan-out and BCs no longer carry GPS — this BN-220 line is a documentation residue. The BC firmware does not initialize I2C for a GPS. A small follow-up patch to leaf v1.2 (call it v1.2.1) should strike that line from the topology; this BC v1.1 amendment doesn't modify the leaf protocol but notes the inconsistency here so it isn't lost.
