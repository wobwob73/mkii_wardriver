# 2.4 GHz WiFi Branch — Leaf Protocol v1.2 Amendment

**Version:** 1.2.0
**Date:** 2026-05-29
**Status:** Amendment to `wifi24_leaf_protocol_v1_1.md` (2026-04-04)
**Changes:** Encryption enum extended with OWE and WPA3-Enterprise. Scan-timing wording in §6.4 corrected (passive vs active distinction). WIDS ring-buffer overflow behavior clarified. RSN AKM mapping table expanded.

This document specifies the exact changes to v1.1.0. Section numbers refer to v1.1.0 unless noted. Anything not mentioned here is unchanged. The intent is to bring the spec into line with the implementation choices that were already baked into the (re-derived) firmware and recorded as deviations in `CODE_STATUS.md`.

---

## §3.1.1 Encryption Enum — Replace Table

The wire-protocol `enc` field in `$AP` and `$BC` is now an integer in the range **0–10**.

| Value | Meaning | ESP32-C3 SDK Constant | Firmware Symbol |
|---|---|---|---|
| 0 | Open | `WIFI_AUTH_OPEN` | `LE_OPEN` |
| 1 | WEP | `WIFI_AUTH_WEP` | `LE_WEP` |
| 2 | WPA_PSK | `WIFI_AUTH_WPA_PSK` | `LE_WPA_PSK` |
| 3 | WPA2_PSK | `WIFI_AUTH_WPA2_PSK` | `LE_WPA2_PSK` |
| 4 | WPA_WPA2_PSK | `WIFI_AUTH_WPA_WPA2_PSK` | `LE_WPA_WPA2_PSK` |
| 5 | WPA2_ENTERPRISE | `WIFI_AUTH_WPA2_ENTERPRISE` | `LE_WPA2_ENT` |
| 6 | WPA3_PSK | `WIFI_AUTH_WPA3_PSK` | `LE_WPA3_PSK` |
| 7 | WPA2_WPA3_PSK | `WIFI_AUTH_WPA2_WPA3_PSK` | `LE_WPA2_WPA3_PSK` |
| 8 | Unknown | (fallback) | `LE_UNKNOWN` |
| **9** | **OWE (Enhanced Open)** | `WIFI_AUTH_OWE` | `LE_OWE` |
| **10** | **WPA3_ENTERPRISE** | `WIFI_AUTH_WPA3_ENT_192` (192-bit) or derived from MFP-Required | `LE_WPA3_ENT` |

**Compatibility note:** The receiver-side parser on the Branch Controller MUST accept the full 0–10 range. Implementations that hard-coded a 0–8 bounds check from v1.1.0 will silently reject `enc=9` or `enc=10`. The BC's `enc` field is a single byte everywhere it's stored (dedup table, upstream `$WA`), so no buffer changes are required — only the bounds check widens.

The `wifi_scan` AP-result path uses the SDK constant directly when available (recent ESP-IDF exposes `WIFI_AUTH_OWE`); older SDK versions without that constant should fall through to the WIDS RSN walker, which determines OWE / WPA3-Ent from the AKM suite (see §7.5 update below).

---

## §7.5 RSN AKM Suite Mapping — Replace Subsection

The WIDS Leaf parses the RSN IE (tag 48) and the Vendor WPA IE (tag 221, OUI `00:50:F2:01`) inside the promiscuous callback to populate the `enc` field of `EVENT_BEACON` events. The mapping below replaces the four-row table in v1.1.0 §7.5.

### 7.5.1 RSN IE Structure (Tag 48)

```
RSN IE layout (after the 1-byte tag and 1-byte length):
  Version              : 2 bytes  (always 0x0001)
  Group Cipher OUI     : 4 bytes  (00-0F-AC-xx)
  Pairwise Cipher Cnt  : 2 bytes  (LE)
  Pairwise Cipher List : 4 * count bytes
  AKM Suite Count      : 2 bytes  (LE)
  AKM Suite List       : 4 * count bytes (each 00-0F-AC-xx)
  RSN Capabilities     : 2 bytes  (LE)
  [optional PMKID list, Group Mgmt Cipher OUI for MFP]
```

The IEEE 802.11 OUI for RSN suites is `00:0F:AC`. The byte we care about is the fourth (`xx` above) — the suite type.

### 7.5.2 AKM Suite Type → Encryption Enum

| AKM Type (last byte of 00-0F-AC-xx) | Suite Name | Maps to `enc` |
|---|---|---|
| 1 | 802.1X (no PMK caching) | 5 (WPA2_ENTERPRISE) by default; 10 (WPA3_ENTERPRISE) if **MFP Required** bit is set in RSN Capabilities |
| 2 | PSK | 3 (WPA2_PSK) |
| 3 | FT 802.1X | 5 (WPA2_ENTERPRISE) by default; 10 (WPA3_ENTERPRISE) if MFP Required |
| 4 | FT PSK | 3 (WPA2_PSK) |
| 5 | 802.1X SHA256 | 10 (WPA3_ENTERPRISE) — SHA256 implies WPA3-Ent transition or required |
| 6 | PSK SHA256 | 3 (WPA2_PSK) — treated as WPA2 for our purposes |
| 8 | SAE | 6 (WPA3_PSK); combined with AKM 2 → 7 (WPA2_WPA3_PSK transition) |
| 11 | SuiteB 128-bit | 10 (WPA3_ENTERPRISE) |
| 12 | SuiteB 192-bit | 10 (WPA3_ENTERPRISE) — 192-bit GCMP variant |
| 18 | OWE | 9 (OWE) |
| Other (7, 9, 10, 13–17, 19+) | Reserved or FT variants | 5 if 802.1X-related, otherwise 8 (Unknown) |

### 7.5.3 MFP (Management Frame Protection) Lookup

Bits 6 and 7 of byte 0 of **RSN Capabilities** carry:

- Bit 6 = MFPR (Management Frame Protection Required)
- Bit 7 = MFPC (Management Frame Protection Capable)

WPA3-Enterprise transition mode requires MFP to be Capable but not Required; WPA3-Enterprise (mandatory) requires MFPR. The walker reads RSN Capabilities to disambiguate AKM 1 / AKM 3 between WPA2-Ent and WPA3-Ent.

### 7.5.4 Combination Rules

The walker iterates the AKM list and accumulates a bitmask of seen AKMs, then picks `enc` by precedence:

1. If AKM 18 only → `enc = 9` (OWE).
2. If AKM 8 and (AKM 2 or AKM 6) → `enc = 7` (WPA2_WPA3_PSK transition).
3. Else if AKM 8 → `enc = 6` (WPA3_PSK).
4. Else if (AKM 5 or AKM 11 or AKM 12) or ((AKM 1 or AKM 3) and MFPR) → `enc = 10` (WPA3_ENTERPRISE).
5. Else if (AKM 1 or AKM 3) → `enc = 5` (WPA2_ENTERPRISE).
6. Else if (AKM 2 or AKM 4 or AKM 6) → `enc = 3` (WPA2_PSK).
7. Else if Vendor WPA IE present (OUI `00:50:F2`, type 1) and no RSN IE → `enc = 2` (WPA_PSK); if BOTH WPA1 and RSN present → `enc = 4` (WPA_WPA2_PSK).
8. Else if no RSN/WPA IE but Capability Info Privacy bit (offset 34, bit 4) is set → `enc = 1` (WEP).
9. Else → `enc = 0` (Open). The OWE detection rule (1) takes precedence over the Open fallback.
10. If AKM list parses but matches none of the above → `enc = 8` (Unknown).

---

## §6.4 Implementation Notes — Replace Scan Timing Paragraph

Replace the v1.1.0 paragraph:

> **Scan timing:** Single-channel scans on ESP32-C3 take 100–120 ms. Do not add delays between cycles. The scan function itself provides the pacing.

with:

> **Scan timing — active vs passive:** The ESP32-C3 WiFi scan API distinguishes active (transmit probe requests, wait for probe responses) and passive (listen only) modes. The MKII platform is **passive listen-only by design** — no association, no probes, no transmission. The W1–W3 scan Leaves call `WiFi.scanNetworks(async=true, show_hidden=true, channel=N, passive=true, max_ms_per_chan=200)` or the equivalent ESP-IDF call.
>
> | Mode | Per-channel time | Notes |
> |---|---|---|
> | Active (probe-driven, NOT used) | 100–120 ms | Faster but transmits probe requests. Disallowed by MKII's passive philosophy. |
> | Passive (listen for beacons) | ~200 ms typical | Minimum is one full beacon interval (~102 ms at standard 100 TU); 200 ms gives margin for one beacon plus jitter. |
>
> Net result: a single-channel passive scan completes roughly every 200–250 ms including the SDK's housekeeping. `$AP`/`$BK` cadence is therefore ~4 batches/s per scan Leaf, not the ~9/s implied by the v1.1.0 100 ms figure. The spec examples elsewhere that assume a faster cadence are informative only; the firmware paces itself off the SDK and does not need a hard limit.

Note: there is no behavioral change to the Leaf firmware required for this update; it just brings the spec's timing language into line with what the listen-only firmware already does.

---

## §7.4 WIDS Ring Buffer — Replace Overflow Paragraph

Replace the v1.1.0 paragraph:

> **Overflow:** If write index catches read index, the oldest event is overwritten. The main loop increments `err_count` when it detects the gap. This is acceptable — transient event loss during high-activity bursts is preferable to blocking the WiFi task.

with:

> **Overflow:** If write index would catch read index, the **newest** event is dropped — the producer (the promiscuous callback) returns without writing. The ring's `dropped_count` is incremented; the main loop drains `dropped_count` into the Leaf's `err_count` on each cycle and surfaces it in `$HB`.
>
> Rationale: dropping the newest event keeps already-buffered events safe from being overwritten and removes a class of TOCTOU race between the producer (callback context) and the consumer (loop context). Functionally either policy is acceptable for loss accounting — burst loss is reported either way — but newest-drop is simpler to prove correct on a single-core MCU and is what the implementation does.
>
> Either policy is acceptable for v1.2; the spec records newest-drop as the canonical implementation. Implementations that drop oldest must still increment `err_count` per drop.

The struct, capacity, and SPSC concurrency model in v1.1.0 §7.4 are unchanged.

---

## §10 Build Configuration — Update Version Macro

Change `-DLEAF_VERSION=\"1.1.0\"` to `-DLEAF_VERSION=\"1.2.0\"` in `platformio.ini`. No other build flags change.

The fallback default in `leaf_defs.h` (used when the build flag is not set) follows the same change.

---

## §13 Open Items — Reduced

The following items from v1.1.0 §13 are addressed by this amendment and may be marked **resolved** in the next consolidated rewrite:

| # | v1.1.0 status | v1.2 status |
|---|---|---|
| 1 | RP2040 Branch Controller firmware guide — Next | Done (see `branch_controller_wifi24_v1_0.md`, with v1.1 amendment pending) |
| 4 | RP2040 evil twin detection logic — Not yet specified | Covered by `branch_controller_wifi24_v1_0.md` §6 (WIDS analysis) |
| 5 | RP2040 deauth flood thresholds — Not yet specified | Covered by `branch_controller_wifi24_v1_0.md` §6 (WIDS analysis) |

Items 2, 3, 6, 7, 8 from v1.1.0 §13 remain open.

---

## Summary of All Changes

| Section | Change Type | Description |
|---------|-------------|-------------|
| §3.1.1 | Replace table | Adds `enc=9` (OWE) and `enc=10` (WPA3_ENTERPRISE); receiver bound widened 0–10 |
| §7.5 | Replace subsection | Full AKM mapping incl. SHA256, FT, OWE, SuiteB; MFP-based WPA2-Ent vs WPA3-Ent disambiguation; combination-precedence rules |
| §6.4 | Replace paragraph | Passive scan ~200 ms/ch; spec calibrated against MKII's listen-only design |
| §7.4 | Replace paragraph | WIDS ring overflow drops newest event (matches implementation) |
| §10 | Update macro | `LEAF_VERSION` → 1.2.0 |
| §13 | Update status | Items 1/4/5 from v1.1.0 marked resolved |

---

## Cross-Reference Resolution

This amendment resolves all three "candidates for a v1.2 amendment" listed under `leaf_wifi24` in `CODE_STATUS.md` §1 ("Deviations from spec v1.1.0"). After v1.2 is incorporated, those deviations are no longer deviations — they are spec-conformant behavior.

The document version ladder for the WiFi 2.4 GHz Leaf protocol is now:
`wifi24_leaf_protocol_v1_1.md` (2026-04-04) → `wifi24_leaf_protocol_v1_2_amendment.md` (2026-05-29). Read both together; the amendment only restates the sections it changes.
