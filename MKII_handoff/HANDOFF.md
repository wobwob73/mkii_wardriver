# MKII — Multi-Spectrum RF Survey Platform — Handoff Package

**Compiled:** 2026-05-28
**Purpose:** Single-document handoff covering project goals, system architecture,
current state, completed work, outstanding work, repository layout, and a precise
inventory of produced code (including what lives outside this package and how to
retrieve it). Intended to bring a code-side contributor up to speed with no prior
context.

> **Read this first.** Two firmware codebases have already been written and
> compile/flash successfully, but their source archives are **not in this package** —
> they were generated in prior working sessions and saved to ephemeral output
> directories. See [§7 Code Inventory](#7-code-inventory--what-exists-and-where) for
> exactly what exists and how to pull it. This document and the `specs/` folder are
> the authoritative design reference regardless.

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
**Env Sensor Branch → STM32:** `$EN` (env record, 10 Hz, 17 fields), `$SB` (status).

Full field tables in `specs/wifi24_leaf_protocol_v1_1.md` and
`specs/branch_controller_wifi24_v1_0.md`.

---

## 5. Current State

### 5.1 Built and working

- **2.4 GHz WiFi Branch — Leaf firmware (`leaf_wifi24`).** ESP32-C3, PlatformIO/Arduino,
  11 source files. Single binary for all 4 Leaves; identity adopted from first `$CF`.
  W1/W2/W3 park on channels 1/6/11 (beacon overlap captures all 11 US channels — see
  §6); W4 is the WIDS Leaf, hops 1–14 (incl. US-unauthorized 12–14) for evil-twin and
  deauth-flood detection. **Compiles and flashes successfully** (confirmed in VSCode/
  PlatformIO).
- **2.4 GHz WiFi Branch — Branch Controller firmware (`branch_wifi24`).** RP2040,
  pico-sdk/CMake, 29 files, ~3,100 lines. Dual-core: Core 0 = 4× PIO UART Leaf links
  with self-chaining DMA ring buffers + Leaf health watchdog; Core 1 = 1PPS timing,
  dedup (500 ms windows, tombstone eviction), WIDS analysis, upstream TX to STM32.
  True SPSC queues, no spinlocks. **Builds to UF2.**

### 5.2 Designed, specced, not built

- **Environmental Sensor Branch.** Single RP2040, no Leaf tier; direct I2C to
  ICM-42688-P (IMU), LIS3MDL (mag), BMP390 (baro), SCD41 (CO2/RH/temp), SGP41 (VOC).
  *(SHT40 dropped — subsumed by SCD41.)* Madgwick fusion at 100 Hz, decimated `$EN`
  at 10 Hz. Identified as the simplest next Branch.
- **Analyzer application.** Fork SSA as infrastructure base; gut GNSS-specific
  schema/routes; port `analysis_engine.py` and `data_sources.py` from Wardriving
  Analyzer as new modules. React/TS/Vite frontend, SQLite, Leaflet, Recharts, PDF
  theming. Not started against MKII.
- **All other Branches:** 5 GHz WiFi, BLE/BT, 802.15.4, Meshtastic, VHF/UHF ISM, FPV.
- **STM32 firmware (both units).** Nothing written yet — see TODO.
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

> Critical for the code-side handoff. The two firmware archives below are **real,
> complete, and known-good**, but they are **not bundled in this package**. They were
> produced in earlier sessions and written to a sandbox output directory that does not
> persist. Retrieve them from the source conversations (links below) or re-export them,
> then drop them into version control. Do **not** attempt to reconstruct them from spec
> text — the specs describe intent, the archives are the implementation, and they
> contain deliberate, documented deviations from the specs.

See `CODE_STATUS.md` in this package for the per-file breakdown, build/flash
instructions, and the full list of spec deviations baked into each archive.

| Archive | Target | Build | Status | Where to get it |
|---|---|---|---|---|
| `leaf_wifi24.zip` | ESP32-C3 | PlatformIO/Arduino | Flashes OK | Chat "ESP32-C6 WiFi development code" (a5d7ac7c) — `present_files` output |
| `branch_wifi24.zip` | RP2040 | pico-sdk/CMake → UF2 | Builds OK | Same chat (a5d7ac7c) — `present_files` output |

Neither the SSA fork nor the Wardriving Analyzer codebase is in this package; both
are external prior codebases referenced as the analyzer's starting point.

---

## 8. Outstanding Work (TODO)

Ordered roughly by the build sequence (software phase gates hardware).

### Immediate (2.4 GHz WiFi Branch bring-up)
1. Retrieve `leaf_wifi24.zip` and `branch_wifi24.zip` into a repo (see §7).
2. Flash the 4 XIAO ESP32-C3 Leaf units; set RF-switch solder bridge to external
   antenna on each; verify GPIO pinout against the BC firmware pin map.
3. Bench-test BC + 1 Leaf, then BC + 4 Leaves (procedures in
   `specs/branch_controller_wifi24_v1_0.md` §13 and
   `specs/wifi24_leaf_protocol_v1_1.md` §11).

### STM32 firmware (blocking for any Branch→Trunk path) — none written yet
4. `$TM` generation after each PPS edge.
5. `$RC` relay passthrough to the correct Branch UART.
6. Branch UART parsing + SD-card write format (standalone mode).
7. USB-CDC streaming to the Trunk.

### Next Branch
8. Build the Environmental Sensor Branch (simplest; lives on STM32 #1's USART6).

### Analyzer
9. Fork SSA; gut GNSS schema/routes; port `analysis_engine.py` + `data_sources.py`.
10. Stand up the SQLite schema (DDL in the specs) + CSV import pipeline.

### SDR (Jetson)
11. rtl_433 as a sidecar for known-device decoding + GPS-tagging wrapper + DB ingest
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

These are stale references the code side should not trust blindly:

1. **STM32 board changed; docs not fully updated.** The current board is the
   **NUCLEO-H753ZI** (144-pin **STM32H753ZIT6**, no onboard SDRAM, all 8 UARTs
   accessible), purchased via DigiKey from an authorized source. The prior
   **FK743M2-IIT6** (STM32H743IIT6) was **abandoned** — its onboard SDRAM pin conflicts
   blocked UART3/7/8 and the SD card conflicted on UART5. However:
   - `specs/system_plan_v2.md` §6 and `specs/system_plan_v2_1_amendment.md` §6 still
     list `"device": "FK743M2-IIT6"` in the `stm32_units` block.
   - `hardware_reference/canvas.png` and `hardware_reference/FK743LAYOUT.pdf` document
     the **abandoned** FK743 board. They're retained as historical reference only;
     they do **not** describe the current hardware.
   - Note the part change is H743→H753 (adds crypto; otherwise pin-compatible family).
   **Action:** when STM32 firmware/specs are next revised, update the board name and
   regenerate pinout against the NUCLEO-H753ZI.

2. **Env Sensor sensor set changed.** Spec amendment §3.8 lists SHT40 for
   thermo/humidity. Current design **drops SHT40** (subsumed by **SCD41**) and adds
   **SGP41** (VOC). The amendment text is stale on this point.

3. **`specs/Wardriving_Project_Snapshot_1_.md` is pre-MKII history.** It documents the
   *previous* ESP32-S3/ESP8266 wardriving rig and the Wardriving Analyzer v4.4.11. It's
   included for lineage/context only — it is **not** the MKII design. Where it conflicts
   with `system_plan_v2*`, the v2 docs win.

4. **Document version ladder.** Authoritative order: `system_plan_v2.md` (2026-04-03)
   → `system_plan_v2_1_amendment.md` (2026-04-04, amends specific sections of v2). The
   amendment does not restate unchanged sections; read both together.

---

## 10. Package Layout

```
MKII_handoff/
├── HANDOFF.md                          # this document — start here
├── CODE_STATUS.md                      # firmware inventory, build/flash, deviations
├── specs/
│   ├── system_plan_v2.md               # authoritative architecture (2026-04-03)
│   ├── system_plan_v2_1_amendment.md   # 1PPS + Env Branch + ESP32-C3 (2026-04-04)
│   ├── branch_controller_wifi24_v1_0.md# RP2040 BC firmware guide
│   ├── wifi24_leaf_protocol_v1_1.md    # Leaf↔BC protocol + Leaf firmware guide
│   └── Wardriving_Project_Snapshot_1_.md # pre-MKII history (context only)
└── hardware_reference/
    ├── canvas.png                      # FK743M2-IIT6 mechanical drawing (ABANDONED board)
    └── FK743LAYOUT.pdf                 # FK743M2-IIT6 pin layout (ABANDONED board)
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
