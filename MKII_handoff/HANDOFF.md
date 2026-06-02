# MKII — Multi-Spectrum RF Survey Platform — Handoff Package

**Compiled:** 2026-06-02
**Purpose:** Single-document handoff covering project goals, system architecture,
current state, completed work, outstanding work, repository layout, and a precise
inventory of produced code. Intended to bring a code-side contributor up to speed with
no prior context.

> **Read this first.** Six firmware trees are written and committed under `firmware/`,
> each with its own GitHub Actions build workflow: `leaf_wifi24`, `branch_wifi24`,
> `leaf_wifi5`, `branch_wifi5`, `env_sensor_branch`, and `stm32_aggregator`. Nothing is
> "missing / retrieve from chat" anymore. `leaf_wifi24` and `branch_wifi24` are
> re-derivations from spec (the original source was lost with a prior sandbox). See
> [§7 Code Inventory](#7-code-inventory--what-exists-and-where) and the companion
> `CODE_STATUS.md` for per-tree status, versions, build commands, and deviations. This
> document and `specs/` remain the authoritative design reference.

---

## 1. What MKII Is

MKII is a vehicle-mounted, **passive** multi-spectrum RF survey and collection
platform. It listens across WiFi (2.4/5 GHz), BLE/BT Classic, 802.15.4
(Thread/Zigbee/Matter), sub-GHz ISM (315/433/868/915 MHz), LoRa (Meshtastic/Meshcore),
FPV video/control links, and P25 public-safety radio, correlating every detection
with GPS position and time. Output is a queryable database feeding an analyzer
application (mapping, classification, device-signature matching, reporting).

It is a ground-up redesign succeeding two prior codebases — **Silver Streak Suite
(SSA, v5.12.0)** and **Wardriving Analyzer (v4.8.0)** — which serve as inspiration,
not direct ancestors. SSA is being forked as the analyzer's infrastructure base.

### Core design philosophy

- **Passive, listen-only.** No transmission, no association, no decryption — presence
  and metadata only.
- **Specification-first.** Formal protocol specs, firmware guides, and architecture
  docs are produced before or alongside code. Everything in `specs/` predates its
  implementation.
- **Modular, branch-by-branch.** Each "Branch" is independently buildable and
  testable. Software phases gate hardware construction: build a Branch only once its
  analyzer support is ready.
- **No single catastrophic point of failure by design choice.** Aggregation chains
  are kept shallow; daisy-chain topologies were explicitly rejected (see §6).

---

## 2. System Architecture

### 2.1 The tree metaphor (hardware hierarchy)

```
ROOT     Analyzer software (laptop / desktop / GCS)
  │
TRUNK    Jetson Orin Nano — heavy compute
  │      ├── SQLite primary data store
  │      ├── SDR P25 monitoring (trunk-recorder) + Whisper GPU voice-to-text
  │      ├── reference GPS (u-blox X20 / F9P), authoritative position/time
  │      └── USB hub → both STM32 units + 6–8 RTL-SDR dongles
  │
  ├── STM32 #1  (RF Collection)   ──USB CDC──→ Trunk
  │     GPS: M10Q-5883 (I2C) → 1PPS fan-out to its Branches
  │     ├── BRANCH 2.4 GHz WiFi    RP2040 BC + 4× ESP32-C3
  │     ├── BRANCH 5 GHz WiFi      RP2040 BC + 2–3× ESP32-C5
  │     ├── BRANCH BLE/BT          RP2040 BC + 2× ESP32-S3
  │     ├── BRANCH 802.15.4        RP2040 BC + 2–4× ESP32-H2
  │     └── BRANCH Env Sensors     RP2040 BC + IMU/Mag/Baro/CO2+VOC
  │
  └── STM32 #2  (Sub-GHz / FPV)   ──USB CDC──→ Trunk
        GPS: M10 basic (I2C) → 1PPS fan-out to its Branches
        ├── BRANCH Meshtastic/Meshcore  RP2040 BC + 5× LoRa Leaves
        ├── BRANCH VHF ISM (315/433)    RP2040 BC + 6× Arduino Nano + FSK RX
        ├── BRANCH UHF ISM (868/915)    RP2040 BC + 8× mixed Leaves
        └── BRANCH FPV Detection        RP2040 BC + RX5808 + LoRa sniffers
```

| Tier | Term | Hardware | Role |
|---|---|---|---|
| Leaf | radio endpoint | ESP32-C3/C5/S3/H2, Arduino Nano, LoRa, RX5808 | Single-protocol capture |
| Branch Controller (BC) | aggregator | RP2040 | Manages a protocol-family group of Leaves via PIO UART |
| Branch | assembly | BC + its Leaves | Independently testable unit |
| STM32 | mid-tier aggregator | NUCLEO-H753ZI | Manages multiple Branches, owns GPS + 1PPS |
| Trunk | heavy compute | Jetson Orin Nano | SDR, voice-to-text, primary DB, reference GPS |
| Root | analyzer | host PC | Query, map, classify, report |

### 2.2 Timing — 1PPS distribution (v2.1, supersedes per-Branch GPS)

Branch Controllers carry **no GPS**. Each STM32's GPS provides a hardware 1PPS edge
(±30 ns) fanned out to every attached RP2040's GPIO. After each edge the STM32 sends a
`$TM,epoch_s,fix_ok` message on each Branch UART. The RP2040 associates the epoch with
its local `time_us_64()` capture of the edge and interpolates sub-second time from its
crystal (±20 ppm → ±20 µs/s).

- **Accuracy:** ±20 µs vs. 50–80 ms NMEA latency for per-Branch GPS.
- **Cost/complexity:** eliminates 8× BN-220 (~$80) and frees a PIO UART per Branch.
- **PPS loss:** detected after 2 s; RP2040 free-runs from last epoch, sets
  `time_flag=1` (degraded) on all records until PPS resumes.
- **Fan-out:** push-pull CMOS 1PPS drives 4–5 GPIO inputs directly; an SN74LVC1G17
  Schmitt buffer boosts fan-out if more Branches are added.

### 2.3 Operating modes

| Mode | Trunk? | Behavior |
|---|---|---|
| Standalone | no | Each STM32 logs to its own SD; import later into the analyzer |
| Connected | yes | STM32s stream via USB CDC; Jetson writes unified SQLite, runs SDR/voice |
| Minimal | no, one STM32 | Subset of Branches for handheld/bicycle use |

---

## 3. Data Model (SQLite)

The analyzer persists to SQLite. Primary `detections` table (40+ columns) holds the
universal "identifier + name + RSSI + channel" shape (WiFi, BLE, BT Classic,
802.15.4, HaLow). Protocols with fundamentally different shapes get their own tables
to avoid dozens of NULL columns per row. ~10 tables total:

| Table | Source | Notes |
|---|---|---|
| `detections` | WiFi/BLE/BT/802.15.4/HaLow | Universal RF shape, 40+ columns |
| `environment_records` | Env Sensor Branch | 10 Hz, ~36k rows/hr — separate by rate + shape |
| `lora_detections` | Meshtastic/Meshcore + UHF LoRa | SF/BW/CR, protocol classification, node/hop/channel-hash |
| `subghz_detections` | 315/433/868/915 FSK Leaves | modulation, protocol, raw code, repeat count |
| `fpv_detections` | RX5808 scanner | channel/band name, RSSI, analog/digital_suspected |
| `radio_transcripts` | SDR + Whisper | talkgroup, duration, transcript, confidence, audio path |
| `aprs_beacons` | RTL-SDR + direwolf (future) | callsign, symbol, path |
| `sessions` | metadata | session-level config JSON |

Full DDL for the protocol tables and `environment_records` is in
`specs/system_plan_v2.md` §5 and `specs/system_plan_v2_1_amendment.md` §5.

Key analyzer features (planned/ported from SSA + Wardriving Analyzer):
DBSCAN static/mobile classification; device-signature DB (Flock Safety ALPR via BLE
company ID `0x09C8`, Raven gunshot detectors via BLE service UUID); Pattern-of-Life
correlation (on-demand only, whitelist/blacklist); IoT-audit PDF reports; WiGLE
export; GPS jamming/interference detection.

---

## 4. Messaging Protocols (NMEA-style across all UART links)

All Leaf↔BC and BC↔STM32 links use ASCII framing: `$TYPE,f1,f2,...*XX\n` where `XX`
is the XOR checksum of bytes between `$` and `*`, uppercase hex. 230,400 baud, 8N1.
SSIDs hex-encoded (can contain arbitrary bytes). Max line 200 bytes. No ACK, no
retransmit — corrupted lines dropped, heartbeats expose error rates.

**Leaf → BC (2.4 GHz WiFi):** `$AP` (AP detection), `$BK` (batch end), `$DE`
(deauth/disassoc), `$PR` (probe), `$BC` (beacon, WIDS), `$HB` (heartbeat).
**BC → Leaf:** `$CF` (config), `$CH` (WIDS channel bitmask), `$PG` (ping), `$RB` (reboot).
**BC → STM32:** `$WA` (deduped AP), `$WP` (probe), `$ET` (evil twin), `$DF` (deauth
flood), `$BS` (branch status).
**STM32 → BC:** `$TM` (time epoch), `$RC` (relay command — transparent passthrough to
a Leaf), `$RQ` (request status).
**Env Sensor Branch → STM32:** `$EN` (env record, 10 Hz, 20 fields per `system_plan_v2_2_amendment.md`), `$SB` (status).

Full field tables in `specs/wifi24_leaf_protocol_v1_1.md` and
`specs/branch_controller_wifi24_v1_0.md`.

---

## 5. Current State

### 5.1 Written and building in CI

All six firmware trees are committed under `firmware/` and build in their per-tree
GitHub Actions workflows. Per-tree versions, build commands, and deviation logs are in
`CODE_STATUS.md`.

- **2.4 GHz WiFi — `leaf_wifi24`** (ESP32-C3, PlatformIO/Arduino, FW `1.2.0`) and
  **`branch_wifi24`** (RP2040, pico-sdk → UF2, FW `1.1.0`). The original 2.4 GHz source
  was lost with a prior sandbox; both are **re-derived from spec** (`wifi24_leaf_protocol_v1_1`
  + v1.2 amendment; `branch_controller_wifi24_v1_0` + v1.1 amendment) and build in CI.
- **5 GHz WiFi — `leaf_wifi5`** (ESP32-C5, PlatformIO/pioarduino, FW `1.0.1`) and
  **`branch_wifi5`** (RP2040, pico-sdk → UF2, FW `1.0.0`). A band-select bug that kept the
  C5 on 2.4 GHz (a `#ifdef` on a non-macro enumerator) was fixed in leaf `1.0.1`.
- **Environmental Sensor Branch — `env_sensor_branch`** (RP2040, pico-sdk → UF2, FW
  `1.0.0`). Direct I2C to ICM-42688-P, LIS3MDL, BMP390, SCD41, SGP41 *(SHT40 dropped —
  subsumed by SCD41)*. Madgwick fusion at 100 Hz, decimated `$EN` at 10 Hz. The Sensirion
  Gas Index Algorithm is a baseline-only stub in v1.0.x (VOC/NOx are constants until v1.1
  — see `CODE_STATUS.md` §5).
- **STM32 mid-tier — `stm32_aggregator`** (NUCLEO-H753ZI ×2, bare-metal HAL, FW `1.0.1`).
  Portable `App/` over a PAL; `Core/` hand-codes the clock/peripheral init. CI builds both
  the host-smoke library and the on-target `.elf`/`.bin` for both units. The clock tree
  was corrected to a coherent 480/240/120 MHz this pass (with I2C `TIMINGR` and TIM2
  prescaler recomputed). `gps_push_config()` is a no-op; the GPS is pre-configured via
  u-center (documented; UBX-at-boot deferred to v1.1).

### 5.2 Not built yet

- **The six remaining protocol Branches** (Leaf + BC firmware each): BLE/BT (ESP32-S3),
  802.15.4 (ESP32-H2), Meshtastic/Meshcore (LoRa), VHF ISM (315/433), UHF ISM (868/915),
  FPV (RX5808). Specs exist under `specs/`.
- **Analyzer application.** Fork SSA as infrastructure base; gut GNSS-specific
  schema/routes; port `analysis_engine.py` and `data_sources.py` from Wardriving
  Analyzer. React/TS/Vite, SQLite, Leaflet, Recharts, PDF theming. Not started.
- **Jetson Trunk** software (SDR sidecar, Whisper, DB writer, USB-CDC ingest).

### 5.3 Antenna / hardware decisions locked

- Seeed XIAO ESP32-C3 boards chosen for the 4 WiFi Leaves (factory U.FL connector,
  avoids RF feed rework). Action: set RF-switch solder bridge to external-antenna mode
  on all 4; verify GPIO pinout vs. BC firmware pin map.
- External patch antennas (Taoglas, sourced separately): directional 60–90°, ~6–9 dBi,
  ~2.44 GHz center; forward-facing wedge geometry. Patch radiation pattern produces the
  wedge without extra hardware.

---

## 6. Key Decisions & Learnings (so a reviewer doesn't relitigate them)

- **Channel physics.** Scan Leaves on 1/6/11 see beacons from all 11 US channels via
  spectral overlap — per-channel scanning for the 3 scan Leaves is unnecessary.
- **ESP8266 fully removed.** Frozen SDK, no WPA3, limited promiscuous mode, 80 KB RAM.
  Superseded by ESP32-C3 (400 KB RAM, WPA3, full promiscuous frame access).
- **ESP32-H2 has no WiFi radio** — unsuitable for WiFi Branches (it's for 802.15.4).
- **1PPS fan-out beats per-Branch GPS** on cost *and* accuracy (see §2.2).
- **SPSC queues: no spinlocks.** Classic single-producer/single-consumer ring with
  separate 32-bit-aligned indices + `__dmb()` barriers is correct and faster. Spec
  §4.1's "spinlock" wording is in tension with "lock-free SPSC"; firmware implements
  true SPSC. Flagged for spec v1.1 clarification.
- **Tombstone-based dedup eviction** avoids a double-increment bug present in
  rehash-on-eviction.
- **Avoid aggregation chains that create single points of failure.** An RP2040 BC
  crash taking down its Leaves is an *accepted* known risk; compounding it with
  daisy-chains (e.g., the rejected ESP8266 UART-relay) is not.
- **No manual antenna feed rework on production units without VNA verification** —
  factory U.FL preferred.
- **rtl_433 limits:** no native GPS correlation, no MKII schema integration, silent on
  unknown protocols — needs a wrapper + ingest adapter.

---

## 7. Code Inventory — What Exists and Where

All firmware is committed in this repository under `firmware/`. See `CODE_STATUS.md` for
the per-file breakdown, build commands, versions, and the deviation log per tree.

| Tree | Target | Build | Version | CI |
|---|---|---|---|---|
| `firmware/leaf_wifi24` | ESP32-C3 | PlatformIO/Arduino | `1.2.0` | `leaf_wifi24.yml` |
| `firmware/branch_wifi24` | RP2040 | pico-sdk/CMake → UF2 | `1.1.0` | `branch_wifi24.yml` |
| `firmware/leaf_wifi5` | ESP32-C5 | PlatformIO/pioarduino → Arduino | `1.0.1` | `leaf_wifi5.yml` |
| `firmware/branch_wifi5` | RP2040 | pico-sdk/CMake → UF2 | `1.0.0` | `branch_wifi5.yml` |
| `firmware/env_sensor_branch` | RP2040 | pico-sdk/CMake → UF2 | `1.0.0` | `env_sensor_branch.yml` |
| `firmware/stm32_aggregator` | NUCLEO-H753ZI ×2 | bare-metal HAL (CMake) + host-smoke | `1.0.1` | `stm32_aggregator.yml` |

`leaf_wifi24` and `branch_wifi24` are **re-derivations from spec** — the original 2.4 GHz
source was lost with a prior sandbox, so they were rebuilt from `wifi24_leaf_protocol_v1_1.md`
+ v1.2 amendment and `branch_controller_wifi24_v1_0.md` + v1.1 amendment, and version-bumped
to fold those amendments in. Their correctness rests on CI compilation plus bench bring-up,
**not** the prior manual VSCode/PlatformIO confirmation.

Neither the SSA fork nor the Wardriving Analyzer codebase is in this repository; both are
external prior codebases referenced as the analyzer's starting point.

---

## 8. Outstanding Work (TODO)

Ordered roughly by the build sequence (software phase gates hardware). The firmware that
was "to write" in the prior handoff (STM32 aggregator, Env Sensor, 5 GHz WiFi) is now
written and building in CI; the work ahead is bench bring-up plus the six unbuilt Branches.

### Bench bring-up of the written firmware
1. **STM32 aggregator:** flash both unit images; verify the 480 MHz clock, 400 kHz I2C
   SCL, and 1 MHz TIM2 tick on a scope (`specs/stm32_h753_firmware_v1_0.md` §13); then
   GPS-only (`$AG`/`$TM`/PPS) with a **u-center-preconfigured** M10 (the firmware does not
   push UBX config — see the STM32 README §GPS pre-configuration).
2. **2.4 GHz + 5 GHz WiFi Branches:** flash Leaves + BC; verify the leaf_wifi5 band-select
   fix puts the C5 on 5 GHz; bench-test BC + 1 Leaf then BC + N Leaves (spec §13/§11).
   On the C3 Leaves set the RF-switch solder bridge to external antenna; verify GPIO pinout
   vs. the BC firmware pin map.
3. **Env Sensor Branch:** verify `$EN`/`$SB`; treat `voc`/`nox` as baseline-only until the
   v1.1 gas-index integration.

### Six unbuilt protocol Branches (Leaf + BC each)
4. BLE/BT (ESP32-S3), 802.15.4 (ESP32-H2), Meshtastic/Meshcore (LoRa), VHF ISM (315/433),
   UHF ISM (868/915), FPV (RX5808). Specs are in `specs/`.

### Analyzer
5. Fork SSA; gut GNSS schema/routes; port `analysis_engine.py` + `data_sources.py`.
6. Stand up the SQLite schema (DDL in the specs) + CSV import pipeline.

### SDR (Jetson)
7. rtl_433 as a sidecar for known-device decoding + GPS-tagging wrapper + DB ingest
   adapter (small-scope items already identified).

### Open hardware questions (not yet resolved)
- Power regulation/budget for RP2040 + 4× ESP32-C3 (not analyzed).
- 1PPS signal integrity over wiring to multiple RP2040s (verify at assembly).
- Final GPIO pin assignments (preliminary pending PCB layout).
- Evil-twin threshold tuning for enterprise multi-AP environments (field).
- Dedup window duration (500 ms baseline, `#define`, needs field tuning).
- BladeRF 2.0 micro xA9 under consideration to replace the RTL-SDR bank (adds 5/5.8 GHz
  energy detection).
- RX5808 SPI-modded modules needed for FPV (RotorHazard-compatible as a quality signal;
  AKK Diversity Receiver confirmed unsuitable).

---

## 9. ⚠ Version / Consistency Issues to Resolve

### Resolved (review-driven hardening pass, 2026-06-02)

Triggered by the external code-analysis report (`mkii_code_analysis_report.md`,
2026-06-01). All confirmed defects in the Critical / High tiers are addressed in
written firmware; the deferred items are recorded with version targets below.

10. **PIO TX retarget (F-001).** The single shared TX state machine on both
    BC trees now updates its `PINCTRL.OUT_BASE` / `SIDESET_BASE` during retarget
    instead of only re-pinning the GPIO. Drain + 80 µs settle + reconfig + JMP
    back to program origin + read-back self-check. Without this fix every
    downstream byte left on the first leaf's TX pin and W2..W4 / W5_2..W5_3
    never received their `$CF`. branch_wifi24 → 1.2.0, branch_wifi5 → 1.1.0.
11. **`$RC` relay (F-002).** Wire format redefined to a hex-encoded inner:
    `$RC,<target>,<hex>*<outer_cksum>`. The plaintext-nested form was
    unparseable on the wire. New BC spec amendment:
    `branch_controller_wifi24_v1_2_amendment.md`. STM32 routes the outer line
    after strict outer validation; BC decodes hex, validates inner, relays.
12. **GPS NMEA validation (F-003).** stm32 firmware now rejects sentences that
    fail NMEA checksum or contain out-of-range time/date fields before any
    update of the timebase. A bad I2C byte can no longer poison `$TM`.
13. **Atomic PPS snapshots (F-004).** All four trees now read time/state via
    a `pps_time_snapshot()` accessor with a hardware spinlock (RP2040) or a
    PRIMASK-protected critical section (STM32). 64-bit `pps_timer_us` cannot
    tear; apply/ISR cannot interleave.
14. **TIM2 64-bit overflow race (F-005).** `pal_time_us_64()` now consults
    the TIM2 UIF flag during the read and folds the carry in software when an
    overflow has fired but the ISR hasn't yet bumped the high half — closes a
    ~71-min backward-jump window near PPS edges.
15. **Strict outer framing (F-006).** All proto consumers now require `*XX`
    at the exact end of the line; line receivers strip a trailing CR.
16. **WIDS correctness (F-007 partial).** `ET_ENC_MISMATCH` now emitted when
    SSID matches but encryption differs; single-radio alerts no longer
    populate `known_bssid` with a copy of `rogue_bssid` (analyzer can now tell
    "no second radio identified" from a future two-radio case). Full
    SSID-keyed evil-twin redesign is deferred.
17. **Bounded leaf UART TX (F-008).** Replaced the unbounded
    "spin until `availableForWrite() ≥ n`" with chunked writes + 50 ms
    deadline + drop counter in `$HB`.

### Second-review delta (2026-06-02)

A second external review of the same branch re-discovered most of the same
items already on the deferred list. The one finding whose reachability changed
because of F-001 — `$CF` mode validation in both leaf trees — was pulled
forward and is now in `leaf_wifi24 1.2.2` / `leaf_wifi5 1.0.3`:
`adopt_from_cf()` now rejects mode values outside the declared `LeafMode`
enum at the boundary, so a corrupt or future-extended `$CF` cannot place a
leaf into an unknown state. Existing `hb::note_error()` path increments the
error counter.

The other second-review findings (per-leaf RX init rc, ESP `esp_wifi_*` rc,
SDK fragility, branch identity-vs-slot mismatch, FetchContent network need)
were already covered by F-012 / F-016 / documented design choices and remain
on the v1.1 plan.

### Deferred review items (open-items registers; v1.1+ targets)

Tracked but not implemented; recorded so they don't get lost:

- **F-007 full evil-twin redesign** (SSID-keyed tracking, security downgrade
  detection). Target: BC v1.2+. W4 WIDS bench bring-up is the gating event.
- **F-009** invalid hex SSID payloads collapsing to "empty SSID" rather than
  being rejected. Target: BC v1.2+.
- **F-012** ignored init/runtime return codes (`pio_uart_subsys_init`,
  `pio_uart_rx_init` per-leaf, `esp_wifi_*` return codes,
  `HAL_UART_Receive_DMA`, `pal_sd_open_append`). Target: per-tree v1.1+.
- **F-013** SD heartbeat reports OK if mounted even when no log files opened.
  Target: STM32 v1.1.
- **F-016** numeric parser hardening (strict end-of-conversion checks, range
  limits on channel / encryption / sat-count fields). `$CF` mode validation
  resolved 2026-06-02 — the rest of F-016 remains. Target: cross-tree v1.1.
- **Branch identity-vs-slot mismatch** (second review #10): the BC trusts
  the physical UART slot's index more than the reported leaf ID. Adding a
  mismatch counter is a diagnostic improvement, not a correctness bug.
  Target: BC v1.2+.
- **F-014** STM32 `gps_push_config()` no-op (u-center pre-config required);
  already documented (`stm32_h753_firmware_v1_0.md` §14 item 2).
- **F-015** SGP41 baseline-only gas-index stub; already documented
  (`env_sensor_branch_v1_0.md` §14 item 9).
- **F-017–F-023** operational hardening (CI/toolchain pin SHAs, USB CDC DTR
  gating + RX-overflow counter, W5 scanner first-pass skip, dispatch
  unknown-prefix discard, D-cache placement if later enabled, USB VID/PID
  finalization). Target: pre-production cleanup.

### Earlier resolutions (2026-06-01 stabilization pass)

1. **STM32 board (FK743 → NUCLEO-H753ZI).** The FK743M2-IIT6 references in
   `system_plan_v2.md` and `system_plan_v2_1_amendment.md` `stm32_units` blocks now carry
   dated inline **correction notes** pointing to the NUCLEO-H753ZI (STM32H753ZIT6); the
   authoritative hardware is defined in `stm32_h753_firmware_v1_0.md`. History is preserved
   (the JSON is left in place under the note). `hardware_reference/canvas.png` and
   `FK743LAYOUT.pdf` remain, **labeled abandoned** (see §10).
2. **Env Sensor sensor set (SHT40 → SCD41 + SGP41).** Resolved by
   `system_plan_v2_2_amendment.md`; the v2.1 SHT40 text is superseded.
3. **STM32 clock tree.** `SystemClock_Config` now yields a coherent 480/240/120 MHz tree;
   the I2C1 `TIMINGR` (now genuine 400 kHz @ 120 MHz PCLK1) and TIM2 prescaler (1 MHz tick)
   were recomputed to match. STM32 FW bumped to `1.0.1`.
4. **5 GHz band-select bug.** `leaf_wifi5` no longer compiles out `esp_wifi_set_band` via a
   dead `#ifdef`; FW bumped to `1.0.1`.
5. **leaf_wifi5 README platform claim.** Now correctly states the pioarduino fork (matching
   `platformio.ini`), not mainline `espressif32`.

### Open placeholders (recorded, not silent — both targeted v1.1)

6. **STM32 `gps_push_config()` is a no-op.** GPS must be pre-configured via u-center
   (STM32 README §GPS pre-configuration; `stm32_h753_firmware_v1_0.md` §14 item 2).
7. **Env Sensor SGP41 Gas Index Algorithm is a stub.** `$EN` `voc`/`nox` are baseline-only
   constants until integrated (`env_sensor_branch_v1_0.md` §14 item 9).

### Still genuinely stale / context-only

8. **`specs/Wardriving_Project_Snapshot_1_.md` is pre-MKII history** (ESP32-S3/ESP8266 rig,
   Wardriving Analyzer v4.4.11). Lineage/context only; where it conflicts with
   `system_plan_v2*`, the v2 docs win. (It still mentions FK743 in its historical context —
   left as-is, since it is a snapshot of prior history, not current design.)

9. **Document version ladder.** Authoritative order: `system_plan_v2.md` (2026-04-03)
   → `system_plan_v2_1_amendment.md` (2026-04-04) → `system_plan_v2_2_amendment.md`
   (2026-05-29). Each amendment restates only the sections it changes; read all three
   together.

---

## 10. Package Layout

```
<repo root>/
├── firmware/                           # all six firmware trees (each builds in CI)
│   ├── leaf_wifi24/        ESP32-C3   PlatformIO/Arduino       FW 1.2.0
│   ├── branch_wifi24/      RP2040     pico-sdk/CMake → UF2     FW 1.1.0
│   ├── leaf_wifi5/         ESP32-C5   PlatformIO/pioarduino    FW 1.0.1
│   ├── branch_wifi5/       RP2040     pico-sdk/CMake → UF2     FW 1.0.0
│   ├── env_sensor_branch/  RP2040     pico-sdk/CMake → UF2     FW 1.0.0
│   └── stm32_aggregator/   H753ZI ×2  bare-metal HAL + host-smoke  FW 1.0.1
├── .github/workflows/                  # one build workflow per firmware tree
└── MKII_handoff/
    ├── HANDOFF.md                      # this document — start here
    ├── CODE_STATUS.md                  # firmware inventory, build, versions, deviations
    ├── specs/
    │   ├── system_plan_v2.md                  # authoritative architecture (2026-04-03)
    │   ├── system_plan_v2_1_amendment.md      # 1PPS + Env Branch + ESP32-C3 (2026-04-04)
    │   ├── system_plan_v2_2_amendment.md      # SCD41 + SGP41 sensor set, $EN v2 (2026-05-29)
    │   ├── stm32_h753_firmware_v1_0.md         # STM32 aggregator spec (NUCLEO-H753ZI)
    │   ├── wifi24_leaf_protocol_v1_1.md + v1_2_amendment.md   # 2.4 GHz Leaf protocol
    │   ├── branch_controller_wifi24_v1_0.md + v1_1_amendment.md  # 2.4 GHz BC guide
    │   ├── wifi5_branch_v1_0.md                # 5 GHz Leaf + BC guide
    │   ├── env_sensor_branch_v1_0.md           # Env Sensor Branch guide
    │   ├── blebt / dot154 / meshtastic / vhf_ism / uhf_ism / fpv _branch_v1_0.md  # unbuilt Branches
    │   └── Wardriving_Project_Snapshot_1_.md   # pre-MKII history (context only)
    └── hardware_reference/             # ABANDONED FK743 board — historical only
        ├── canvas.png                  # FK743M2-IIT6 mechanical drawing (ABANDONED)
        └── FK743LAYOUT.pdf             # FK743M2-IIT6 pin layout (ABANDONED)
```

---

## 11. Tooling Reference

- **ESP32 firmware:** VSCode + PlatformIO; ESP-IDF for ESP32-H2 branches; Arduino
  framework for C3.
- **RP2040 firmware:** pico-sdk + CMake; Raspberry Pi Pico VSCode extension (bundles
  SDK/toolchain, Import Project, compile/flash/debug). SWD debug via a second RP2040
  running `debugprobe` — recommended for dual-core bring-up where a hung core is
  otherwise invisible. `arm-none-eabi-gcc` toolchain.
- **Analyzer stack:** React/TypeScript/Vite, SQLite, Leaflet, Recharts, ReportLab-style
  PDF theming (forked from SSA).
- **SDR:** rtl_433 (known-device decode), trunk-recorder (P25), Whisper (voice-to-text)
  on the Jetson.
- **Sourcing:** DigiKey for authorized STM32 Nucleo boards; RotorHazard compatibility
  as a vendor quality signal for RX5808 modules.
