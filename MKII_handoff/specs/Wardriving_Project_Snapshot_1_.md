# Wardriving Project — State Snapshot

**Generated:** 2026-03-25  
**Purpose:** Capture the full state of the wardriving rig hardware/firmware project and the Wardriving Analyzer software tool, including where the ESP32-S3 → STM32 conversion was left off. This document is intended as input context for a fresh planning conversation.

---

## 1. Project Overview

The wardriving rig is a multi-spectrum RF survey platform with two major components:

1. **Hardware Rig** — A collection of microcontrollers (ESP8266, ESP32-S3, ESP32-H2, HT-HC33) with a central aggregator that coordinates scanning, correlates GPS timestamps, and writes WiGLE-compatible CSV to an SD card.

2. **Wardriving Analyzer** — A Python/Flask web application (v4.4.11) that ingests the CSV output from the rig, classifies signals as static vs mobile using DBSCAN clustering and RSSI analysis, and provides map/table visualization, playback, exports (KML/GeoJSON/CSV), and PDF/HTML reports.

---

## 2. Hardware Rig — Architecture History

### Phase 1: Original ESP32-S3 "Driver" Architecture

The original rig used an **ESP32-S3-DevkitC-1 V1.4** (gold contacts variant) as the central aggregator ("Driver"). It coordinated all workers, performed its own BLE scanning, parsed GPS, and wrote to a SPI-mode SD card.

**Workers:**

| Worker ID | Device       | Function                  | Channels/Bands     |
|-----------|-------------|---------------------------|---------------------|
| A         | ESP8266     | WiFi 2.4GHz scanning      | Ch 1–3              |
| B         | ESP8266     | WiFi 2.4GHz scanning      | Ch 4–6              |
| C         | ESP8266     | WiFi 2.4GHz scanning      | Ch 7–9              |
| D         | ESP8266     | WiFi 2.4GHz scanning      | Ch 10–14            |

The four ESP8266 workers are soldered in place and the user explicitly prefers not to desolder/reprogram them.

**Driver (ESP32-S3) responsibilities at this stage:**
- UART coordination with all workers (broadcast SCAN, collect responses)
- BLE scanning using internal radio
- GPS parsing (u-blox M10Q via UART at 9600 baud)
- SD card writes (SPI mode: CS=GPIO10, SCK=GPIO12, MOSI=GPIO11, MISO=GPIO13)
- CSV generation with WiGLE-compatible format

**Communication protocol (ESP32-S3 era):**
- Boot baud: 9600
- Target baud: 38400 (max for ESP8266 workers at this stage)
- Handshake: Driver sends `SETBAUD:XXXXX`, worker responds `BAUDOK:XXXXX`
- Scan cycle: Driver broadcasts `SCAN`, workers respond with lines like `X,SSID,BSSID,RSSI,CHANNEL,ENCRYPTION` then `X,DONE`
- Scan period: 250ms (4 Hz) for high-speed capture (50–120 mph)

### Phase 2: Expansion (IMU + New Workers)

Additional devices were added to the ESP32-S3 driver:

| Worker ID | Device       | Function                  | Notes               |
|-----------|-------------|---------------------------|----------------------|
| E         | ESP32-H2    | Thread/Zigbee/Matter      | 802.15.4 passive scanning |
| F         | HT-HC33     | WiFi HaLow (802.11ah)    | Sub-1GHz, no interference with 2.4GHz |
| M         | ESP32-S3 (Driver itself) | BT/BLE scanning | Internal radio, dual-core allows async scan |
| I (data)  | MPU-9250/6500 IMU | Heading/orientation  | Connected via ESP32-H2 (Worker E handles IMU reads) |

**Expanded CSV schema (ESP32-S3 era):**
```
iso8601_utc,lat,lon,alt_m,hdop,sats,fix_ok,heading_deg,pitch_deg,roll_deg,
accel_x_mg,accel_y_mg,accel_z_mg,signal_type,worker,identifier,name,
rssi_dbm,channel,extra1,extra2,extra3
```

**Worker message formats:**

| Worker | Format |
|--------|--------|
| A–D (WiFi) | `X,SSID,BSSID,RSSI,CHANNEL,ENCRYPTION` |
| E (Thread/Zigbee) | `E,PROTOCOL,EUI64,PANID,RSSI,CHANNEL,TYPE` |
| F (HaLow) | `F,SSID,BSSID,RSSI,ENC` |
| M (BLE) | Internal — written directly as CSV rows |
| I (IMU) | `I,HEADING,PITCH,ROLL,AX,AY,AZ,TIMESTAMP` (disabled at time of STM32 transition) |

### Phase 3: STM32H743 Aggregator Upgrade (WHERE IT WAS LEFT OFF)

The decision was made to replace the ESP32-S3 as the aggregator with an **STM32H743IIT6** (EC Buying FK743M2-IIT6 V1.1 board). The ESP32-S3 would then become a dedicated BLE worker (Worker M), no longer responsible for coordination.

**Why STM32H743IIT6 was chosen:**
- 480 MHz Cortex-M7, 2MB Flash, 1MB RAM
- 8 hardware UARTs with DMA (vs ESP32-S3's 3)
- Native SDMMC 4-bit SDIO (much faster than SPI SD)
- 176-pin LQFP (hand-solderable, unlike the BGA variant)
- Enough GPIO and peripherals for significant future expansion

**Planned STM32 UART assignments:**

| UART    | STM32 Pins  | Connected To          | Baud Rate  |
|---------|-------------|----------------------|------------|
| USART1  | PA9/PA10    | Worker A (ESP8266)   | 230400     |
| USART2  | PA2/PA3     | Worker B (ESP8266)   | 230400     |
| USART3  | PD8/PD9     | Worker C (ESP8266)   | 230400     |
| UART4   | PA0/PA1     | Worker D (ESP8266)   | 230400     |
| UART5   | PC12/PD2    | Worker M (ESP32-S3 BLE) | 230400  |
| USART6  | PC6/PC7     | Worker E (ESP32-H2)  | 230400     |
| UART7   | PE8/PE7     | Worker F (HT-HC33)   | 230400     |
| UART8   | PE1/PE0     | GPS (u-blox M10Q)   | 115200     |

**Planned storage:**
- Primary: Onboard SDMMC1 (4-bit SDIO) — significantly faster than SPI mode
- Backup: External SPI SD module via SPI2 (PB13/PB14/PB15)

**Planned GPS configuration:**
- u-blox M10Q at 10Hz with GPS+GLONASS+Galileo
- STM32 configures M10 at startup via UBX commands
- GNSS CSV schema designed (extended WiGLE format with UBX fields)

**Additional architectural decisions made during planning:**

1. **SN74LVC244AN octal buffer** was to be added between STM32 TX broadcast and all WiFi workers (A–D share a single TX line from STM32, each has individual RX back). This enables the higher 230400 baud safely.

2. **Hub/Aggregator piggybacking** — Instead of using additional STM32 UARTs for future devices:
   - ESP32-S3 (Worker M) acts as a local hub: BLE scanning + piggybacked BT Classic scanner (second ESP32-S3) + future Meshtastic detector via UART2
   - HT-HC33 (Worker F) acts as a hub: HaLow scanning + piggybacked LoRa/Meshtastic detector
   - I2C reserved for physical sensors (GPS fallback, IMU, barometer)

3. **Dedicated BT Classic** — A second ESP32-S3 (Worker N) was planned for continuous BT Classic inquiry, since ESP32 cannot do BLE and BT Classic simultaneously (shared radio). GPS could move to I2C to free UART8 for Worker N.

4. **Matter detection** — The ESP32-H2 (Worker E) detects Matter devices passively because Matter runs over Thread (802.15.4). Matter devices are identifiable by specific characteristics in their advertisements. No additional hardware needed.

---

## 3. Where the STM32 Conversion Was Left Off

### What Was Completed

- **Full system architecture** designed and documented (UART assignments, pin maps, wiring tables for every device pair)
- **STM32 aggregator firmware (v2.0.0)** written in C for PlatformIO (STM32Cube HAL):
  - DMA-based UART RX ring buffers for all 8 UARTs
  - Worker handshake protocol (SETBAUD → BAUDOK)
  - GPS UBX parser (NAV-PVT, NAV-SAT) with startup configuration
  - Dual SD card support (SDMMC primary, SPI backup)
  - WiGLE CSV generation + GNSS CSV generation
  - File rotation (size/age/date triggers)
  - USB CDC debug output
  - Watchdog monitoring per worker
- **All worker firmwares** written:
  - ESP8266 Workers A–D (.ino, Arduino framework)
  - ESP32-S3 BLE Worker M (.ino, Arduino framework) — dedicated BLE + BT Classic scanner
  - ESP32-H2 Thread/Zigbee Worker E (.ino, ESP-IDF framework via PlatformIO)
  - HT-HC33 HaLow Worker F (.ino, Arduino framework)
  - ESP32-S3 BT Classic Worker N (.ino, Arduino framework) — planned future addition
- **CSV schemas documented:**
  - RF detection CSV (WiGLE-compatible with extended columns)
  - GNSS CSV (extended WiGLE + raw UBX fields: 31 columns including per-constellation sat counts, accuracy estimates, heading, velocity components)
- **Throughput analysis** performed — STM32 has massive headroom. Worst-case dense urban: ~15 KB/s aggregate input vs 23 KB/s per-UART capacity at 230400 baud. SD write at SDIO 4-bit easily handles this.

### What Was NOT Completed / Remains Open

1. **No firmware was flashed or tested** — All code was generated but the user had not yet begun hardware assembly or flashing.
2. **ESP32-H2 compilation issue** — The Worker E firmware uses `esp_ieee802154.h` (ESP-IDF component), which doesn't compile in the Arduino framework. Needs PlatformIO with ESP-IDF framework, not Arduino. This was identified but the resolution (PlatformIO project configuration) was described, not tested.
3. **No physical wiring done** — Wiring guide was produced but assembly had not started.
4. **ESP8266 worker reprogramming question** — The existing ESP8266 workers were at 38400 baud max. The STM32 firmware was designed to handshake at 9600 then negotiate up to 230400, but the existing ESP8266 firmware on the soldered-in units may not support 230400. New firmware was written but flashing requires desoldering.
5. **I2C GPS fallback** not implemented — Was identified as an option to free UART8 for Worker N (BT Classic), but code was not written for I2C GPS communication.
6. **Meshtastic integration** designed but not coded — The piggybacking architecture was planned (Meshtastic on ESP32-S3 UART2, or on HT-HC33 UART2) but no firmware was written for the Meshtastic parser.
7. **IMU was disabled/toasted** — The user's primary IMU was damaged at the time of the STM32 transition. IMU data columns exist in the CSV schema but IMU handling was not a priority.

---

## 4. Technical Gotchas & Implementation Warnings

These are issues identified during the design phase or discovered through analysis that whoever picks this up next should be aware of. None of these are showstoppers, but ignoring them will waste time.

### 4.1 Shared TX Bus Architecture

Workers A–D do **not** each get a dedicated TX line from the aggregator. The STM32 broadcasts `SCAN` on a single TX wire that fans out to all four ESP8266 RX pins. Each ESP8266 has its own individual TX back to a dedicated STM32 UART RX. The **SN74LVC244AN octal buffer** on that shared TX line is not optional at 230400 baud — without it, signal integrity degrades with four loads on the line. At the original 38400 baud it might work without the buffer, but at 230400 it won't be reliable.

### 4.2 STM32 Firmware Is Untested First-Draft Code

The aggregator firmware (~2000+ lines of bare-metal STM32Cube HAL with DMA ring buffers) was generated in a single conversation pass. It was **never compiled, flashed, or tested**. Treat it as a detailed design reference and starting point, not production code. Expect bugs in DMA initialization, buffer management, and edge-case handling (partial lines, buffer overflows, simultaneous worker responses).

### 4.3 Analyzer ↔ Rig CSV Compatibility Gap

The analyzer's default profile (`v42_default.json`) maps columns like `identifier`, `rssi_dbm`, `iso8601_utc`, etc. — these match the **RF detection CSV**. However, the STM32 design produces **two separate CSV files**:
- RF detection CSV (WiGLE-compatible + extended columns) — the analyzer can ingest this
- GNSS CSV (31-column extended WiGLE + raw UBX fields) — **the analyzer cannot ingest this at all**

If unified analysis of RF detections correlated with high-resolution GNSS data is desired, that's a feature gap that needs explicit planning.

### 4.4 Physical Form Factor & Antenna Considerations

The ESP8266 array is mounted flat in parallel (`I-I-I-I` pattern). Design conversations recommended **polarization diversity** for better capture rates:
- Workers A+B vertical, Workers C+D horizontal
- Or all workers at 45° as a compromise

The rig mounts on a vehicle or RC car at **50–120 mph**, so vibration, power stability, and connection robustness all matter. Dupont wires and breadboard connections will fail at these speeds. Soldered connections or locking connectors (JST, Molex) are recommended for anything that sees road vibration.

### 4.5 Hardware Model Numbers (Exact)

For procurement and datasheet reference:
- **Aggregator (current):** ESP32-S3-DevkitC-1 V1.4 (gold contacts)
- **Aggregator (planned):** EC Buying FK743M2-IIT6 V1.1 (STM32H743IIT6, 176-pin LQFP)
- **WiFi Workers A–D:** ESP8266 (specific module variant not documented — likely ESP-12F or NodeMCU, needs verification)
- **GPS:** Matek M10Q-5883 (u-blox M10 + HMC5883L compass)
- **BLE/BT:** ESP32-S3-DevkitC-1 (same model as current aggregator, repurposed)
- **Thread/Zigbee:** ESP32-H2-DevKitM-1
- **HaLow:** HT-HC33 (Heltec, internally an ESP32-S3 + HT-HC01 HaLow module)
- **IMU (damaged):** MPU-9250 or MPU-6500 (GY-9250 breakout)
- **TX Buffer (planned):** SN74LVC244AN octal buffer

### 4.6 Power Budget Was Never Analyzed

Seven worker MCUs + GPS + two SD cards + STM32 all running simultaneously. No one calculated total current draw or verified the power supply can handle it. Rough estimates:

| Device | Typical Draw | Peak Draw | Notes |
|--------|-------------|-----------|-------|
| ESP8266 × 4 | 70 mA each (~280 mA) | 170 mA each (~680 mA) | Peak during WiFi TX (scan responses) |
| ESP32-S3 | 150 mA | 350 mA | BLE scanning + UART |
| ESP32-H2 | 80 mA | 130 mA | 802.15.4 scanning |
| HT-HC33 | 150 mA | 300 mA | ESP32-S3 internally + HaLow radio |
| STM32H743 | 200 mA | 300 mA | Running all UARTs + SDMMC + DMA |
| M10Q GPS | 25 mA | 35 mA | 10Hz continuous |
| SD cards × 2 | 50 mA each (~100 mA) | 200 mA each (~400 mA) | Peak during writes |
| **TOTAL** | **~985 mA** | **~2,195 mA** | |

A 2.5A supply would be marginal. A 3A+ supply with clean regulation is recommended. If running from vehicle 12V, a quality buck converter to 5V (then 3.3V LDO per device or group) is needed. This needs real measurement once assembled.

### 4.7 ESP32-H2 Framework Is a Real Blocker

The 802.15.4 radio API (`esp_ieee802154.h`) is **ESP-IDF only**. The `.ino` file that was generated will not compile in Arduino IDE. The documented fix is PlatformIO with `framework = espidf` in `platformio.ini`, but this was **never actually tested**. The ESP32-H2 has limited community support compared to the S3, so expect to spend time troubleshooting the toolchain.

### 4.8 No UART Error Handling or Checksums

The worker ↔ aggregator protocol is plain ASCII over UART with no CRC, no checksum, no retry mechanism. If a byte is corrupted (noise, EMI, loose connection at 120 mph), the CSV line is silently corrupted. At 38400 baud over short runs this is rarely an issue. At 230400 baud with vibration and longer wiring runs, it becomes more likely. Options to consider:
- Add a simple checksum (XOR or CRC8) to each worker response line
- Add line-level validation in the aggregator (expected field count, value range checks)
- Accept the risk for now and rely on post-processing cleanup in the analyzer

### 4.9 HaLow Library Availability Is Uncertain

The HT-HC33 Worker F firmware references `#include "HaLow.h"` — a Heltec-provided library. At the time of design, the actual availability and maturity of this library for passive scanning was **not verified**. Heltec's HaLow support has historically been limited. The firmware includes a fallback to regular 2.4GHz WiFi scanning if the HaLow module doesn't initialize, but that defeats the purpose of the HT-HC33.

**Action needed:** Verify that Heltec provides a working HaLow scanning library for the HT-HC33. Check their GitHub (`https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series` or similar) and forums. If no scanning library exists, the HT-HC33 may need to be driven at a lower level via AT commands to the HT-HC01 sub-module.

### 4.10 Time Synchronization Across Workers

Workers don't have their own real-time clocks. The aggregator timestamps each detection with GPS time at the moment it processes the worker's response — not at the moment the worker actually heard the signal. At 250ms scan cycles and 230400 baud, the latency between "worker detects AP" and "aggregator writes timestamp" could be 10–100ms. At 120 mph (54 m/s), that's 0.5–5 meters of positional error on top of GPS accuracy. This is acceptable for wardriving but worth understanding.

### 4.11 GPS Cold Start Data Gap

On every power-on, the M10Q needs time to achieve satellite lock — typically 30 seconds to 2 minutes depending on conditions, longer if cold start. During this window, all RF detections have `fix_ok=0` and no GPS coordinates. The analyzer handles this (table view for no-GPS data), but it means the first few minutes of every drive session may have RF data with no position.

**Not addressed:** Whether the STM32 firmware should buffer detections during GPS cold start and retroactively assign coordinates once a fix is achieved, or just write them with `fix_ok=0`. The current design does the latter.

### 4.12 SD Card Filesystem Limitations

FAT32 has a 4GB per-file limit. At high scan rates with extended CSV columns, a single session file could theoretically approach this on very long drives. The STM32 firmware includes file rotation (by size, age, or date change), which mitigates this, but the actual rotation thresholds were set at 10MB / 1 hour / midnight UTC — these were design choices, not tested values.

**Not addressed:** exFAT support on STM32. The STM32Cube FatFs middleware supports FAT32 by default. exFAT requires licensing from Microsoft (or use of open-source exFAT implementations). For files under 4GB this doesn't matter, but if file rotation ever fails, FAT32 will silently corrupt data beyond 4GB.

### 4.13 Pin Conflict Verification Not Done

The UART pin assignments (PA9, PA2, PD8, PA0, PC12, PC6, PE8, PE1, etc.) were selected from the STM32H743 datasheet. They were **not verified against the EC Buying FK743M2-IIT6 V1.1 board's actual pin breakout**. Development boards sometimes use certain pins for onboard LEDs, buttons, boot mode selection, external flash, or USB routing. Some planned pins may not be physically accessible on the board's headers, or may conflict with onboard peripherals.

**Action needed:** Obtain or verify the FK743M2-IIT6 V1.1 schematic and pin header layout. Cross-reference every planned UART/SPI/SDMMC pin against what's actually broken out and available. This is a prerequisite — discovering a pin conflict after wiring requires re-assigning UARTs and rewriting firmware initialization.

### 4.14 STM32 SDMMC1 / SD Card Slot Status

It's unclear whether the FK743M2-IIT6 board has an **onboard microSD slot** connected to SDMMC1, or whether the user needs to wire an external SD card breakout to the SDMMC1 pins. This significantly affects the wiring guide and the "backup SPI SD" design. If the board already has an SDMMC slot, the backup SPI SD card may be the external module, and vice versa.

### 4.15 STM32 Programming / Flashing Method

The firmware was written for PlatformIO upload, but the actual method of flashing the FK743M2-IIT6 board was not confirmed:
- Does the board have an onboard ST-Link? (Unlikely at this price point.)
- Does it have a USB DFU boot mode accessible via a button? (Likely.)
- Does it have SWD pads exposed for an external ST-Link/J-Link?
- Which USB port on the board connects to the STM32 USB peripheral for CDC debug output?

The user's comfort level with embedded toolchains is limited — clear, step-by-step flashing instructions are essential.

### 4.16 Worker Timeout and Crash Recovery

The STM32 firmware design includes a watchdog timer per worker (5000ms timeout). If a worker doesn't respond within the timeout, it's marked as offline. However:
- There is no mechanism to **power-cycle** a crashed worker. The STM32 would need a GPIO connected to each worker's reset or enable pin to perform a hardware reset.
- Without hardware reset capability, a crashed worker stays dead until the entire rig is power-cycled.
- The watchdog timeout value (5s) may need tuning — at 250ms scan cycles, a worker that misses 20 consecutive scan cycles is probably hard-crashed, not just slow.

### 4.17 DMA Ring Buffer Overflow Handling

The STM32 firmware uses DMA-based ring buffers for UART RX. If a worker sends more data than the buffer can hold before the aggregator processes it (e.g., in a dense urban environment where all workers are returning hundreds of results simultaneously), the buffer wraps and overwrites unread data. The buffer sizes were set during design but never stress-tested. The firmware should ideally detect overflow conditions (DMA half-transfer and transfer-complete interrupts) and log them rather than silently losing data.

### 4.18 Existing ESP8266 Firmware Is Unverified

The ESP8266 workers that are currently soldered in and running — what firmware are they actually running? It's presumably the original code from the ESP32-S3 driver era, but the exact version, baud rate support, and message format compatibility with the STM32 design were never confirmed by reading back the firmware or testing the handshake. The new ESP8266 firmware was written to match the inferred protocol, but if the actual firmware differs, the handshake will fail silently.

**Action needed:** Before any STM32 integration, connect one ESP8266 worker to a USB-serial adapter, send `SETBAUD:38400` and `SCAN`, and confirm the exact protocol and message format. This costs 10 minutes and eliminates a major unknown.

### 4.19 UART Exhaustion — No Room for Growth

All 8 UARTs on the STM32H743 are allocated (7 workers + GPS). If Worker N (dedicated BT Classic) is added, there is literally no UART left. The proposed solution was to move GPS to I2C, freeing UART8. But if anything else needs a UART in the future (additional sensor, debug port, Meshtastic direct connection, etc.), the system is out of expansion room without an SPI-to-UART bridge chip (e.g., SC16IS752). This should be factored into the architecture if future expansion beyond the current worker set is expected.

### 4.20 "extra1/extra2/extra3" Columns Are Loosely Typed

The RF detection CSV has three generic `extra` columns that carry different semantics depending on the signal type:

| Signal Type | extra1 | extra2 | extra3 |
|-------------|--------|--------|--------|
| WiFi | encryption type | band (2.4/5/6) | — |
| BLE | appearance code | device type | flags |
| BT Classic | COD (class of device) | — | — |
| Thread/Zigbee | PAN ID | device type | — |
| HaLow | bandwidth (MHz) | halow type | — |

This works but is fragile — the analyzer's CSV profile system handles it by mapping to named fields, but anyone writing a new consumer of this CSV needs to know the signal_type to interpret the extra columns correctly. A more explicit schema (named columns per signal type, or a JSON blob) would be cleaner but would break WiGLE compatibility.

---

## 5. Wardriving Analyzer Software (v4.4.11) — Current State

The analysis tool has evolved significantly from its initial KML conversion script through multiple versions to a full-featured Flask web application.

### Architecture

- **Backend:** Python/Flask with Flask-SocketIO (WebSocket support)
- **Frontend:** Embedded single-file HTML/JS/CSS served by Flask (no separate npm build)
- **Storage:** In-memory analysis engine (no database)
- **Optional:** MQTT live ingest via paho-mqtt

### Key Features (v4.4.11)

| Feature | Description |
|---------|-------------|
| CSV Profiles | JSON mapping files that describe how to interpret different CSV formats; user can add custom profiles |
| Multi-file ingest | Upload multiple CSV runs; data accumulates for cross-run analysis |
| DBSCAN classification | Static vs Mobile vs Uncertain using RSSI variance, geographic spread, clustering, multi-run consistency |
| Map View | Dark-mode Leaflet map with markers, selection overlays, observed radius, estimated physical location |
| Table View | Searchable/sortable list (works even without GPS data) |
| Playback | Time-based replay of detections; docked in SOI Info panel |
| Filters | By device type (WiFi/BLE/Thread/HaLow), classification, WiFi security, per-worker enable/disable + opacity |
| Whitelist/Hide | Ignore specific SOIs from display and playback |
| Focus mode | Dim non-selected SOIs by configurable opacity |
| Exports | GeoJSON, KML, CSV |
| PDF/HTML reports | Customizable fonts, company name, watermark logo, overview pie charts (signal type, static/mobile, top-N channels) |
| Route Overview Map | Survey route rendered as PNG in reports (with optional basemap tiles) |
| Settings → About | Shows running version and build timestamp |
| MQTT live ingest | Optional real-time data streaming from the rig |

### File Structure

```
wardriving_analyzer/
├── install.sh              # Single-shot installer (creates venv, installs deps)
├── README.md
├── CHANGELOG.md
└── backend/
    ├── app.py              # ~5000 lines — Flask backend + embedded frontend
    ├── analysis_engine.py  # ~1286 lines — DBSCAN, classification, stats
    ├── data_sources.py     # ~501 lines — Polymorphic CSV/folder/multi-source classes
    ├── mqtt_handler.py     # ~258 lines — MQTT broker connection + data processor
    ├── pdf_report.py       # ~1013 lines — ReportLab PDF + HTML report generation
    ├── route_map.py        # ~235 lines — Survey route overview map image
    ├── requirements.txt
    └── default_profiles/
        ├── v42_default.json
        ├── scan_extended_v1.json
        └── extended_scan_example.json
```

### Analyzer Software Concerns

1. **`app.py` is 5000 lines** with the entire frontend embedded as HTML/JS/CSS strings. This was done to eliminate the npm build step (which was causing deployment issues), but it makes the file difficult to maintain, search, or diff. Any future UI changes require editing inline JavaScript within Python string literals.

2. **No authentication** — The app listens on `0.0.0.0:5000` by default. Anyone on the same network can access it, upload data, and export results. Fine for local analysis on a trusted network, not suitable if exposed.

3. **MQTT has no TLS** — If MQTT live ingest is used, credentials (if set) travel in plaintext. The `.env` template doesn't mention TLS configuration.

4. **Route map tile cache requires internet** — The route overview basemap feature (`route_map.py`) caches map tiles on first use. If the first report generation happens offline (no internet), it falls back to a plain route plot. Not a bug, but the user should be aware.

5. **In-memory storage only** — All analysis state lives in RAM. If the Flask process crashes or restarts, everything is lost. There is no persistence layer, no SQLite, no file-based session state. For a tool that's used to load multiple days of CSV runs, this is a data loss risk.

6. **Tarball hygiene** — The v4.4.11 tarball includes `.git/` directory, `__pycache__/` directories, and an `app.py.bak` file. These should be excluded from distribution tarballs (add a `.tarignore` or build script that excludes them).

7. **No version tags in git** — Version tracking is only via the `APP_VERSION` string in `app.py` and `CHANGELOG.md`. No git tags exist. Makes it impossible to checkout a specific version.

8. **Duplicate changelog entry** — v4.4.9 appears twice in `CHANGELOG.md` with slightly different text. Minor, but indicates the changelog is manually maintained with no validation.

9. **No automated tests** — The analyzer has zero unit tests, integration tests, or end-to-end tests. Every change is manually verified.

10. **Single-threaded Flask in development mode** — The `run.sh` script just calls `python app.py`. There's no gunicorn/uwsgi wrapper, no production WSGI server. Flask's built-in server is single-threaded and not suitable for concurrent users (relevant if MQTT is also active).

### Version History (v4.4.x series)

| Version | Key Changes |
|---------|-------------|
| 4.4.0 | Map hover tooltips, SOI info panel layout fixes |
| 4.4.1–4.4.2 | PDF export font option fixes |
| 4.4.3 | Playback docked in SOI Info panel (replaced popup), REST loading fixes |
| 4.4.4 | Overview pie charts in PDF/HTML reports |
| 4.4.5 | Scrollable settings modal |
| 4.4.6 | Settings modal scroll container fix (no mouse-wheel trap) |
| 4.4.7 | Tabbed settings navigation (General/Report/Charts/Whitelist) |
| 4.4.8 | Settings tab rendering fix for some browsers |
| 4.4.9 | Charts/Whitelist tabs fix (pane nesting/closing-tag issues) |
| 4.4.10 | Survey Route Overview map in reports (+10% bounds padding, tile cache) |
| 4.4.11 | Route Overview basemap settings in UI, About tab with version/build |

### Deployment

- Ubuntu 24 target
- Python 3.10/3.11/3.12
- `./install.sh` → `./run.sh` → http://localhost:5000
- No systemd service in current tarball (was discussed in earlier versions)

---

## 6. Related Conversations (for reference)

| Topic | Chat Link | Key Content |
|-------|-----------|-------------|
| STM32 aggregator upgrade | [28177131](https://claude.ai/chat/28177131-3181-4d18-a2b2-7b2b71eb55b8) | Full STM32 architecture, wiring, firmware, UART assignments, piggybacking design, throughput analysis, CSV schemas |
| Original expansion (IMU + H2 + HC33) | [2b23f8ed](https://claude.ai/chat/2b23f8ed-9f3e-4808-9674-e316944e2045) | Adding ESP32-H2, HT-HC33, IMU to ESP32-S3 driver; compilation issues with ESP32-H2 |
| Wardriving data to KML | [d765f00c](https://claude.ai/chat/d765f00c-32c1-44ea-9add-a0dd11da5afc) | First version of the analyzer tool — DBSCAN clustering, static/mobile classification, full Flask+React app |
| Analyzer bug fixes + MQTT | [0a6c7fa0](https://claude.ai/chat/0a6c7fa0-e560-4b7b-9e02-73fc0662e721) | CSV parsing fixes (mixed line endings), multi-file support, polymorphic data source discussion |
| Mission Planner integration | [e7b40aad](https://claude.ai/chat/e7b40aad-5b3b-4c20-ba9d-974c798e135e) | MAVLink → MQTT bridge for piping Mission Planner GPS data into the analyzer |
| Pi 5 multi-GNSS recorder | [57132eb3](https://claude.ai/chat/57132eb3-e19f-4983-ad9b-0057ce5cc963) | Raspberry Pi 5 recording from 6 GNSS devices, auto-boot/auto-record design |
| STM32 board selection | [0a6c7fa0](https://claude.ai/chat/0a6c7fa0-e560-4b7b-9e02-73fc0662e721) | Comparison of H723ZGT6 vs H743IIT6 vs H743XIH6 — H743IIT6 recommended |

---

## 7. Unknowns & Items Requiring Verification

These are things that were assumed, hand-waved, or never investigated during the design phase. Each should be resolved before committing to implementation.

| # | Item | Status | Impact if Unresolved |
|---|------|--------|----------------------|
| U1 | FK743M2-IIT6 board schematic / actual pin header layout | **Not obtained** | Could invalidate all UART pin assignments — wiring rework + firmware rewrite |
| U2 | FK743M2-IIT6 onboard SD card slot presence and SDMMC connection | **Unknown** | Affects storage architecture and wiring |
| U3 | FK743M2-IIT6 USB port routing (which USB peripheral, CDC support) | **Unknown** | Affects debug output design and user's ability to monitor the system |
| U4 | FK743M2-IIT6 programming method (DFU, SWD pads, onboard debugger) | **Unknown** | Critical for first flash — user cannot proceed without this |
| U5 | Heltec HaLow scanning library availability and maturity | **Not verified** | Could make Worker F useless for its intended purpose |
| U6 | ESP32-H2 PlatformIO + ESP-IDF build actually compiles for 802.15.4 | **Not tested** | Known blocker for Worker E — no Thread/Zigbee scanning without this |
| U7 | Exact ESP8266 module variant on the soldered workers | **Not documented** | Affects available GPIO, flash size, antenna type, max reliable baud |
| U8 | Actual firmware currently running on soldered ESP8266 workers | **Not verified** | Handshake compatibility with STM32 aggregator unknown |
| U9 | Total rig power consumption under load | **Not measured** | Could exceed power supply capacity — intermittent resets, data loss |
| U10 | SD card write speed at SDIO 4-bit on this specific STM32 board | **Not benchmarked** | Assumed fast enough; not proven — could bottleneck in dense environments |
| U11 | WiFi HaLow regulatory compliance for passive scanning in US (902–928 MHz) | **Not researched** | Passive receive is almost certainly legal, but worth confirming |
| U12 | Whether the Matek M10Q-5883 compass (HMC5883L) is usable via I2C simultaneously with GPS | **Not verified** | Relevant if heading data is desired from the compass module |
| U13 | Whether Worker N (second ESP32-S3 for BT Classic) is still desired | **Decision pending** | Determines GPS interface (UART vs I2C) and UART allocation strategy |
| U14 | Whether the existing ESP32-S3 driver board has been repurposed or is available as Worker M | **Unknown** | Need to confirm hardware availability before planning |
| U15 | Logic level compatibility across all devices | **Assumed 3.3V** | ESP8266 GPIO is 3.3V, STM32H743 is 3.3V, ESP32-S3 is 3.3V, M10Q is 3.3V — should be fine, but some ESP8266 modules have 5V-tolerant inputs that could cause confusion if wired to 5V rails |
| U16 | Whether the ESP32-S3 BLE scanner can reliably detect BLE 5.0 extended advertisements | **Not tested** | ESP32-S3 supports BLE 5.0 in hardware, but the Arduino BLE library may not expose extended advertising PDUs. Could miss newer devices. |
| U17 | Actual M10Q UART vs I2C performance at 10Hz | **Not benchmarked** | I2C at 400kHz should handle 10Hz NAV-PVT easily, but if bus is shared with IMU/compass polling, contention could cause missed updates |

---

## 8. Open Questions / Decision Points for Resumption

### Hardware Decisions

1. **Has any hardware been assembled?** The STM32 board, wiring, worker connections — was any physical work done before this project was paused?

2. **ESP8266 reprogramming** — The four soldered-in ESP8266 workers likely need new firmware to support 230400 baud and the STM32's handshake timing. Is desoldering acceptable now, or should the STM32 firmware fall back to 38400 for the existing units and accept the lower data rate?

3. **IMU status** — Was the MPU-9250/6500 replaced? Is IMU data still desired? If so, does it stay on the ESP32-H2 (Worker E) I2C bus, or move to a dedicated I2C connection on the STM32?

4. **GPS I2C vs UART** — The M10Q supports both. If Worker N (BT Classic) is still planned, GPS needs to move to I2C to free UART8. Decision needed. If Worker N is not happening, GPS stays on UART8 and this is moot.

5. **Meshtastic priority** — Is the Meshtastic detector still on the roadmap? If so, which hub (ESP32-S3 or HT-HC33)?

6. **Power supply** — What is the current power source? Vehicle 12V? USB power bank? Dedicated bench supply? This determines whether a power budget analysis is urgent or academic.

7. **Enclosure / mounting** — Is there a physical enclosure? How are connections secured against vibration? This affects whether the SN74LVC244AN buffer and wiring approach need to be more robust than Dupont wires.

8. **Worker N (BT Classic)** — Still wanted? This has cascading implications for GPS interface, UART allocation, and hub architecture.

### Analyzer Decisions

9. **Analyzer stability** — Is v4.4.11 stable and meeting needs, or are there known bugs / desired features?

10. **GNSS CSV ingestion** — Is there a need to visualize/analyze the GNSS CSV data (high-resolution position, velocity, accuracy) in the analyzer? Currently only RF detection CSV is supported.

11. **Live MQTT from STM32** — The STM32 doesn't have WiFi. If live streaming to the analyzer is desired, it would need an additional WiFi bridge (ESP32 running MQTT client, connected to STM32 via UART or SPI). Is this wanted, or is offline CSV analysis sufficient?

12. **Data persistence** — The analyzer is in-memory only. Is the risk of losing a loaded session (process crash, accidental page refresh) acceptable, or should persistence (SQLite, session files) be added?

### Scope Decision

13. **What is being resumed?** Options:
    - (a) Resume the STM32 firmware project from where it left off (verify board, fix pin assignments, compile/flash/test iteratively)
    - (b) Continue evolving the Wardriving Analyzer software
    - (c) Both, but sequenced (e.g., get STM32 producing data first, then evolve analyzer to match)
    - (d) Rethink the architecture entirely (e.g., skip STM32, use Raspberry Pi as aggregator, different approach)

---

## 9. Recommended Pre-Work Before Resuming Implementation

If the STM32 conversion is being resumed, these items should be resolved **before writing or modifying any code:**

### Critical (Blocking)

1. **Obtain the FK743M2-IIT6 V1.1 board schematic.** Verify every planned pin assignment against the board's actual breakout. Identify which pins are used by onboard peripherals (LEDs, buttons, USB, crystal, boot select). This single step could invalidate a large portion of the existing firmware design. Check EC Buying's product page, GitHub, or contact them directly.

2. **Confirm STM32 flashing method.** Determine whether the board supports USB DFU, has SWD pads, or requires an external programmer. Test that PlatformIO can upload a minimal blink program to the board. Do not proceed to aggregator firmware until this works.

3. **Verify ESP32-H2 toolchain.** Create a minimal PlatformIO project with `framework = espidf`, include `esp_ieee802154.h`, and verify it compiles and uploads to the ESP32-H2-DevKitM-1. If this doesn't work, Worker E needs a different approach and the entire 802.15.4 scanning strategy may need revision.

4. **Bench-test the existing ESP8266 workers.** Connect one to a USB-serial adapter, send `SETBAUD:38400` and `SCAN`, and confirm the exact protocol, message format, and baud rate capabilities. This costs 10 minutes and eliminates a major unknown (U8).

### Important (Should Do)

5. **Verify Heltec HaLow library.** Check Heltec's GitHub and forums for a working HaLow scanning API. If none exists, research AT command interface to the HT-HC01 sub-module. This determines whether Worker F firmware needs to be rewritten.

6. **Decide on Worker N and GPS interface.** This cascading decision affects UART allocation, I2C wiring, and firmware for both aggregator and GPS configuration. Make this call before finalizing the architecture.

7. **Rough power budget.** Measure or estimate current draw per device. Select and procure an appropriate power supply. Test with a multimeter under load before trusting it at highway speed.

### Nice to Have (Can Defer)

8. **Antenna orientation plan.** Decide vertical vs horizontal vs mixed polarization for the ESP8266 array. Can be adjusted after initial testing.

9. **Analyzer GNSS CSV support.** Only matters once the STM32 is actually producing GNSS CSV files. Can wait until the rig is operational.

10. **UART checksum protocol.** Can be added later if data corruption is observed in practice. Not worth the complexity upfront.
