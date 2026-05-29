# Multi-Spectrum RF Survey Platform — System Plan v2

**Date:** 2026-04-03
**Status:** Architecture defined, software-first development
**Changes from v1:** Jetson Trunk, dual-STM32, GPS per Branch, expanded sub-GHz/FPV/SDR Branches, Meshtastic/Meshcore, voice-to-text radio monitoring

---

## 1. System Hierarchy (Revised)

```
ROOT ──── Analyzer software (laptop/desktop/GCS)
│
TRUNK ─── Jetson Orin Nano
│         ├── SQLite writer (primary data store)
│         ├── SDR radio monitoring (trunk-recorder + Whisper voice-to-text)
│         ├── APRS decoding (future)
│         ├── SDR waterfall recording (future)
│         ├── Telemetry uplink management
│         ├── GPS: u-blox X20 (reference-grade, I2C or USB)
│         ├── IMU / Compass / Baro (I2C)
│         └── USB hub → all Branches
│
├── STM32 #1 (RF Collection) ──USB CDC──→ Trunk
│   ├── GPS: u-blox M10Q-5883 (I2C)
│   ├── BRANCH: 2.4 GHz WiFi ── RP2040 + 4× ESP8266
│   ├── BRANCH: 5 GHz WiFi ──── RP2040 + 2–3× ESP32-C5
│   ├── BRANCH: BLE/BT ──────── RP2040 + 2× ESP32-S3
│   └── BRANCH: 802.15.4 ────── RP2040 + 2–4× ESP32-H2
│
├── STM32 #2 (Sub-GHz / FPV) ──USB CDC──→ Trunk
│   ├── GPS: u-blox M10 basic (I2C)
│   ├── BRANCH: Meshtastic/Meshcore ── RP2040 + 5× LoRa Leaves
│   ├── BRANCH: VHF ISM (315/433) ──── RP2040 + 6× Arduino Nano + FSK RX
│   ├── BRANCH: UHF ISM (868/915) ──── RP2040 + 8× mixed Leaves
│   └── BRANCH: FPV Detection ──────── RP2040 + RX5808 + LoRa sniffers
│
├── [USB] RTL-SDR × 6–8 (VHF/UHF public safety radio → voice-to-text)
├── [USB] RTL-SDR × 1 (1.3 GHz FPV video detection, future)
├── [USB] RTL-SDR × 1 (APRS 144.390 MHz, future)
└── [USB] Flipper Zero (experimental only, not permanent)
```

### GPS Tiering

| Level | Device | Cost | Purpose |
|-------|--------|------|---------|
| Branch Controller (RP2040) | BN-220 or Beitian BN-180 (u-blox M8) | ~$10 | Local timestamping of detections at point of capture. Reduces positional error from UART transit latency. Enables standalone Branch testing. |
| STM32 #1 | Matek M10Q-5883 (u-blox M10 + compass) | ~$25 | Mid-tier position reference. Compass for heading. Local SD card backup with GPS-stamped data. |
| STM32 #2 | u-blox M10 basic (SAM-M10Q or BN-880Q) | ~$15 | Same role as STM32 #1 for the sub-GHz/FPV side. |
| Trunk (Jetson) | u-blox X20 or F9P | ~$80–200 | Reference-grade position. Multi-band, high-accuracy. Used for environment records, GPS quality analysis, jamming detection. Authoritative timestamp source. |

Each Branch Controller connects its local GPS via I2C or a PIO UART. Detections are timestamped at the Branch level using the local GPS time. The Trunk cross-references Branch timestamps against its own reference GPS. If a Branch GPS has degraded fix quality, the Trunk's timestamp takes precedence.

---

## 2. Full Frequency Landscape

### 2.1 Current Target Bands and Protocols

| Freq Range | Protocols / Signals | Branch | Leaves | Detection Method |
|------------|--------------------|---------|---------|----|
| **315 MHz** | Garage door openers, car key fobs (declining, legacy rolling codes) | VHF ISM | 3× Arduino Nano + ASK/OOK RX | 2 on common garage code freqs, 1 roaming |
| **433 MHz** | Weather stations, TPMS, smart home sensors, older drone C2 (DragonLink, OpenLRS), LoRa 433 | VHF ISM | 3× Arduino Nano + ASK/OOK RX | 2 on common ISM channels, 1 roaming |
| **868 MHz** | ELRS 868 (EU), Crossfire 868, LoRaWAN EU, EnOcean smart home, Meshtastic EU | UHF ISM | See breakdown below | Mixed LoRa + FSK |
| **902–928 MHz** | ELRS 915 (US), Crossfire 915, LoRaWAN US, Z-Wave (908.42), Amazon Sidewalk, WiFi HaLow (802.11ah), Meshtastic US, Meshcore | UHF ISM | See breakdown below | Mixed LoRa + FSK + HaLow |
| **1.3 GHz** | FPV analog video (declining but present) | SDR (Trunk-direct) | RTL-SDR | Energy detection, carrier sensing |
| **2.4 GHz** | WiFi b/g/n/ax, BLE, BT Classic, Thread/Zigbee/Matter, ELRS 2.4, DJI C2, Frsky | WiFi 2.4 + BLE/BT + 802.15.4 | See existing Branches | Protocol-specific scanning |
| **3.3 GHz** | FPV analog video (rare, experimental/military) | Future | HackRF or downconverter | Future milestone |
| **5 GHz** | WiFi a/n/ac/ax | WiFi 5 | ESP32-C5 | WiFi scan API |
| **5.8 GHz** | FPV analog video (dominant), FPV digital (DJI/HDZero/Walksnail), WiFi | FPV Detection | RX5808 + ESP32-C5 overlap | RX5808 RSSI scanning (FPV), WiFi scan (AP detection) |
| **6 GHz** | WiFi 6E (802.11ax) | Future | ESP32-C61 or Intel AX210 via Trunk | Future milestone |
| **144.390 MHz** | APRS ham radio beacons | SDR (Trunk-direct) | RTL-SDR + direwolf | Future milestone |
| **VHF 150–174 MHz** | Public safety voice (police) | SDR (Trunk-direct) | RTL-SDR × 2–4 + trunk-recorder + Whisper | Voice-to-text |
| **UHF 450–470 MHz** | Public safety voice (police/fire/EMS) | SDR (Trunk-direct) | RTL-SDR × 2–4 + trunk-recorder + Whisper | Voice-to-text |

### 2.2 Trend Extrapolation

| Band | Trend | Implication |
|------|-------|-------------|
| 315 MHz | Declining. Modern garage doors use 310/315/390 MHz with rolling codes. Newer systems moving to encrypted protocols or WiFi. | Worth monitoring but diminishing returns over time. |
| 433 MHz | Stable. Huge installed base of cheap ISM sensors. LoRa 433 growing in some regions. Will remain relevant for years. | High-value band for IoT detection. |
| 868/915 MHz | Growing rapidly. LoRaWAN, Meshtastic, ELRS, Sidewalk all expanding. 915 MHz is the most contested sub-GHz band in the US. | Highest priority sub-GHz band. Expect more protocols competing here. |
| 2.4 GHz | Saturated but not declining. WiFi 6 adds more devices. BLE proliferating (trackers, wearables, smart home). Matter/Thread adding more 802.15.4 devices. | Will remain the densest band indefinitely. |
| 5 GHz | Growing. WiFi 5/6 migration ongoing. More APs, cameras, smart home hubs moving to 5 GHz to escape 2.4 GHz congestion. | Critical to capture now. Gap in current rig is unacceptable. |
| 5.8 GHz | FPV analog declining as DJI/HDZero/Walksnail digital takes over, but digital still uses same band. WiFi 5 GHz upper channels overlap. | FPV detection via RX5808 remains valid for analog; digital detection requires different approach (energy/modulation detection). |
| 6 GHz | Early growth. WiFi 6E devices shipping but adoption is still low (~5–10% of new APs). Will accelerate 2027+. | Future milestone. Not urgent but plan for it. |
| ELRS/Crossfire | ELRS 2.4 GHz growing fastest. 900 MHz still used for long-range. Crossfire losing market share to ELRS. | 2.4 GHz LoRa C2 detection matters — overlaps with WiFi/BLE band, need modulation-level detection. |
| Meshtastic | Explosive growth. Community mesh networking on 915 MHz (US) and 868 MHz (EU). Default channels well-documented. | Dedicated detection justified by growth rate. |

---

## 3. Branch Inventory (Complete)

### 3.1 STM32 #1 — RF Collection Branches

**UART budget: 6 usable (USART1, USART2, UART4, UART5, USART6, UART7)**
**Allocation: 4 Branches + GPS (I2C) + 1 spare**

| Branch | UART | Leaves | GPS |
|--------|------|--------|-----|
| 2.4 GHz WiFi | USART1 | 3× ESP8266 (scan ch 1/6/11) + 1× ESP8266 (WIDS promiscuous) | BN-220 on Branch Controller |
| 5 GHz WiFi | USART2 | 2–3× ESP32-C5 (UNII-1 / UNII-3 / optional UNII-2) | BN-220 on Branch Controller |
| BLE/BT | UART4 | ESP32-S3 (BLE scan) + ESP32-S3 (BT Classic inquiry) | BN-220 on Branch Controller |
| 802.15.4 | UART5 | 2–4× ESP32-H2 (Thread/Zigbee/Matter, 802.15.4 ch 11–26) | BN-220 on Branch Controller |
| Spare | USART6 | Reserved for future Branch | — |

STM32 #1 also reads its own GPS (M10Q-5883) via I2C for local backup logging to onboard SD.

### 3.2 STM32 #2 — Sub-GHz / FPV Branches

**UART budget: 6 usable (same pin constraints as STM32 #1 — same board model)**
**Allocation: 4 Branches + GPS (I2C) + 1 spare**

| Branch | UART | Leaves | GPS |
|--------|------|--------|-----|
| Meshtastic / Meshcore | USART1 | 4× Meshtastic + 1× Meshcore (see below) | BN-220 on Branch Controller |
| VHF ISM (315/433 MHz) | USART2 | 3× Arduino Nano @315 + 3× Arduino Nano @433 | BN-220 on Branch Controller |
| UHF ISM (868/915 MHz) | UART4 | See detailed breakdown below | BN-220 on Branch Controller |
| FPV Detection | UART5 | RX5808 scanner + LoRa C2 sniffers (see below) | BN-220 on Branch Controller |
| Spare | USART6 | Reserved | — |

### 3.3 Branch: Meshtastic / Meshcore (Detail)

| Leaf | Device | Frequency | Channel Config | Purpose |
|------|--------|-----------|---------------|---------|
| MESH-1 | Heltec LoRa 32 V3 (ESP32-S3 + SX1262) | 915 MHz | US default (LongFast, ch 20) | Detect standard US Meshtastic traffic |
| MESH-2 | Heltec LoRa 32 V3 | 915 MHz | US alternate primary (varies) | Detect non-default US channels |
| MESH-3 | Heltec LoRa 32 V3 | 868 MHz | EU default | Detect EU Meshtastic traffic |
| MESH-4 | Heltec LoRa 32 V3 | 433 MHz | US 433 defaults | Detect 433 MHz Meshtastic (less common in US) |
| MCORE-1 | Heltec LoRa 32 V3 | 915 MHz | Meshcore default US | Detect Meshcore traffic (different packet format than Meshtastic) |

~$12 per Leaf. 5 Leaves = ~$60.

Firmware: Each Leaf configures its SX1262 to the target frequency/SF/BW/CR and listens for LoRa preambles. On packet detection, it reports: RSSI, SNR, frequency, spreading factor, bandwidth, packet length, and first N bytes of header (enough for the Branch Controller to differentiate Meshtastic from Meshcore from generic LoRaWAN). No decryption attempted — presence detection and metadata only.

### 3.4 Branch: VHF ISM — 315 MHz / 433 MHz (Detail)

| Leaf | Device | Frequency | Purpose |
|------|--------|-----------|---------|
| VHF-315-A | Arduino Nano + superheterodyne ASK RX | 315.000 MHz | Garage door code channel A |
| VHF-315-B | Arduino Nano + superheterodyne ASK RX | 315.000 MHz | Garage door code channel B (diversity / second common freq) |
| VHF-315-R | Arduino Nano + superheterodyne ASK RX | 315 MHz band | Roaming / alternate frequencies |
| VHF-433-A | Arduino Nano + superheterodyne ASK RX | 433.920 MHz | Primary ISM (weather stations, TPMS, sensors) |
| VHF-433-B | Arduino Nano + superheterodyne ASK RX | 433.920 MHz | Diversity / second common channel |
| VHF-433-R | Arduino Nano + superheterodyne ASK RX | 433 MHz band | Roaming / alternate ISM frequencies |

~$5 per Leaf (Nano ~$3 + RX module ~$2). 6 Leaves = ~$30.

Firmware: Arduino reads raw ASK/OOK pulses via interrupt-driven pin monitoring. Decodes common protocols (weather station, TPMS, garage door pulse patterns) using `rc-switch` or custom decoders. Reports: frequency, modulation type, protocol (if identified), raw code/payload, RSSI (if RX module provides it — some superheterodyne modules have RSSI output, some don't; digital RSSI via signal strength comparison may be needed).

### 3.5 Branch: UHF ISM — 868 MHz / 915 MHz (Detail)

This is the most crowded branch. 915 MHz alone hosts HaLow, LoRaWAN, ELRS, Crossfire, Z-Wave, Sidewalk, and generic ISM.

| Leaf | Device | Frequency | Purpose |
|------|--------|-----------|---------|
| UHF-HALOW | HT-HC33 | 902–928 MHz | WiFi HaLow (802.11ah) scanning. No fallback — if library unavailable, Leaf is disabled. |
| UHF-915-IOT-A | Arduino Nano + SX1262 module | 915 MHz | LoRaWAN uplink detection (SF7–12 sweeping) |
| UHF-915-IOT-B | Arduino Nano + SX1262 module | 915 MHz | LoRaWAN downlink / alternate channels |
| UHF-915-ISM-A | Arduino Nano + FSK RX | 915 MHz | Generic FSK ISM (Z-Wave 908.42, Sidewalk, misc) |
| UHF-915-ISM-B | Arduino Nano + FSK RX | 915 MHz | Second ISM listener (diversity / alternate channels) |
| UHF-915-UAS | Arduino Nano + SX1262 module | 915 MHz | Rogue FPV/UAS C2 detection (ELRS/Crossfire LoRa pattern matching) |
| UHF-868-A | Arduino Nano + SX1262 module | 868 MHz | EU ISM / LoRaWAN 868 / ELRS 868 |
| UHF-868-B | Arduino Nano + FSK RX | 868 MHz | Generic FSK 868 (EnOcean, misc EU smart home) |

8 Leaves. Mix of ~$5 (Nano + FSK RX) and ~$10 (Nano + SX1262 module). Total ~$55.

The UAS detection Leaf (UHF-915-UAS) specifically looks for ELRS and Crossfire LoRa packet timing signatures. ELRS uses distinctive SF/BW combinations (e.g., SF6/BW500 for 500Hz mode, SF9/BW500 for 50Hz mode) that differ from standard LoRaWAN (typically SF7–SF12/BW125). The Branch Controller differentiates based on these modulation parameters.

### 3.6 Branch: FPV Detection (Detail)

| Leaf | Device | Band | Purpose |
|------|--------|------|---------|
| FPV-58-A | Arduino Nano + RX5808 module (SPI) | 5.8 GHz | Scan all 48 FPV channels, report RSSI per channel. Active transmitters show as RSSI spikes. |
| FPV-58-B | Arduino Nano + RX5808 module (SPI) | 5.8 GHz | Second scanner for diversity / simultaneous monitoring of detected active channels |
| FPV-LORA-24 | ESP32-C5 or ESP32-S3 + SX1280 | 2.4 GHz | ELRS 2.4 GHz LoRa C2 detection (SX1280 is the 2.4 GHz LoRa transceiver used by ELRS) |
| FPV-LORA-433 | Arduino Nano + SX1262 | 433 MHz | Legacy UHF C2 detection (DragonLink, OpenLRS — declining but present) |

4 Leaves. ~$10–15 each. Total ~$45.

The RX5808 scanner Leaf sweeps all 48 channels (~25ms per channel = ~1.2 second full sweep) and reports active channels (RSSI above noise floor threshold). The Branch Controller maintains a list of active FPV video transmitters with their frequency, RSSI, and first/last seen timestamps.

For DJI/HDZero/Walksnail digital FPV detection on 5.8 GHz: the RX5808 will see energy on the channel (elevated RSSI) but can't decode the digital signal. The detection is "something is transmitting on 5.8 GHz channel X" — which is sufficient for UAS presence detection. Distinguishing analog from digital requires modulation analysis, which is an RTL-SDR/HackRF task (future milestone).

1.3 GHz and 3.3 GHz FPV video detection are handled at the Trunk level via RTL-SDR (1.3 GHz) and HackRF/downconverter (3.3 GHz, future).

### 3.7 SDR Radio Monitoring (Trunk-Direct)

Connects directly to the Jetson via USB. No STM32 or RP2040 involved.

| SDR | Band | Software | Output |
|-----|------|----------|--------|
| RTL-SDR #1 | VHF 150–174 MHz | trunk-recorder (P25 control channel tracker) | Audio files per talk group |
| RTL-SDR #2 | VHF 150–174 MHz | trunk-recorder (voice channel #1) | Audio files |
| RTL-SDR #3 | VHF 150–174 MHz | trunk-recorder (voice channel #2) | Audio files |
| RTL-SDR #4 | UHF 450–470 MHz | trunk-recorder (P25 control channel tracker) | Audio files |
| RTL-SDR #5 | UHF 450–470 MHz | trunk-recorder (voice channel #1) | Audio files |
| RTL-SDR #6 | UHF 450–470 MHz | trunk-recorder (voice channel #2) | Audio files |
| RTL-SDR #7 | 1.3 GHz (future) | Custom energy detector | Carrier presence + RSSI |
| RTL-SDR #8 | 144.390 MHz (future) | direwolf / multimon-ng | Decoded APRS packets |

**Voice-to-text pipeline:**
```
trunk-recorder → WAV audio files (per transmission, tagged with talkgroup/freq/timestamp)
    → Whisper inference on Jetson GPU (small or medium model)
        → Timestamped transcript + confidence score
            → SQLite (radio_transcripts table)
```

Each transcript record: timestamp_utc, frequency_mhz, talkgroup_id, duration_sec, lat, lon (from Trunk GPS at time of reception), transcript_text, whisper_confidence, audio_file_path.

---

## 4. Trunk Architecture

### 4.1 Jetson Orin Nano as Trunk

| Role | Details |
|------|---------|
| USB Hub | Powered 10-port hub. 2× STM32 (CDC), 6–8× RTL-SDR, 1× Flipper (experimental), 1× GPS (if USB), spare ports. |
| Data aggregation | Python daemon reads all STM32 CDC serial ports. Each STM32 produces a unified detection stream. Daemon writes to SQLite. |
| GPS reference | u-blox X20 via USB or I2C (through I2C-to-USB bridge if needed). Polled at 10 Hz. Authoritative position and timestamp. |
| SDR processing | trunk-recorder for P25 trunked radio. Whisper for voice-to-text. Both run as system services. |
| Sensor hub | IMU, compass, barometer via I2C (Jetson has I2C GPIO headers). Alternatively, sensors stay on STM32 #1 and data is forwarded. |
| Storage | NVMe SSD or large SD card. SQLite databases + audio files. 256 GB minimum for multi-day operations with audio. |
| Telemetry uplink | Serial output (USB-to-serial → RF radio to GCS) or WiFi AP for local web dashboard access. |
| Local web UI | Optional: run the Root analyzer directly on the Jetson for in-field live viewing via a tablet connected to the Jetson's WiFi AP. |

### 4.2 STM32 Role (Demoted to "Super-Branch")

Each STM32 operates independently. It can function without the Jetson (standalone mode with SD card logging). When connected to the Jetson, it streams detection records via USB CDC.

Each STM32:
- Has its own GPS (I2C)
- Has its own SD card (SDMMC1, onboard slot)
- Reads its Branches via UART
- Timestamps detections using its local GPS
- Writes to local SD as backup
- Streams formatted detection records to Trunk via USB CDC
- Accepts commands from Trunk via USB CDC (future: Branch reconfiguration, firmware update triggers)

### 4.3 Standalone vs Connected Modes

| Mode | Trunk Present? | Behavior |
|------|---------------|----------|
| **Standalone** | No Jetson | Each STM32 logs to its own SD card. No voice-to-text, no cross-STM32 correlation. Post-collection, SD cards are imported into the Root analyzer. |
| **Connected** | Jetson running | STM32s stream to Jetson in real-time. Jetson writes unified SQLite. SDR processing active. Telemetry uplink available. SD cards still log as backup. |
| **Minimal** | No Jetson, one STM32 only | Single STM32 with a subset of Branches. Viable for handheld/bicycle deployment where weight and power matter. |

---

## 5. Database Schema Additions (v2)

Added to the v1 schema:

```sql
-- Radio transcripts (from SDR voice-to-text pipeline)
CREATE TABLE radio_transcripts (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id      TEXT NOT NULL REFERENCES sessions(session_id),
    timestamp_utc   TEXT NOT NULL,
    frequency_mhz   REAL NOT NULL,
    talkgroup_id    TEXT,
    talkgroup_name  TEXT,           -- user-configurable mapping
    duration_sec    REAL,
    lat             REAL,
    lon             REAL,
    transcript_text TEXT,
    whisper_confidence REAL,        -- 0.0–1.0
    audio_file_path TEXT,           -- relative path to WAV file
    band            TEXT            -- VHF, UHF
);

CREATE INDEX idx_radio_session ON radio_transcripts(session_id);
CREATE INDEX idx_radio_timestamp ON radio_transcripts(timestamp_utc);
CREATE INDEX idx_radio_talkgroup ON radio_transcripts(talkgroup_id);

-- APRS beacons (future)
CREATE TABLE aprs_beacons (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id      TEXT NOT NULL REFERENCES sessions(session_id),
    timestamp_utc   TEXT NOT NULL,
    callsign        TEXT NOT NULL,
    ssid            INTEGER,        -- APRS SSID (0–15)
    lat             REAL,
    lon             REAL,
    symbol          TEXT,           -- APRS symbol code
    comment_text    TEXT,
    path            TEXT,           -- digipeater path
    rx_lat          REAL,           -- rig position at time of reception
    rx_lon          REAL
);

-- FPV video detections (from RX5808 scanner)
CREATE TABLE fpv_detections (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id      TEXT NOT NULL REFERENCES sessions(session_id),
    timestamp_utc   TEXT NOT NULL,
    lat             REAL,
    lon             REAL,
    frequency_mhz   REAL NOT NULL,  -- exact frequency (e.g., 5865 for R7)
    channel_name    TEXT,            -- FPV channel name (e.g., "R7", "F4")
    band_name       TEXT,            -- FPV band name (e.g., "Raceband", "Fatshark")
    rssi_raw        INTEGER,         -- raw RSSI from RX5808
    rssi_dbm        INTEGER,         -- calibrated RSSI (if calibration performed)
    signal_type     TEXT,            -- analog, digital_suspected, unknown
    first_seen_utc  TEXT,
    last_seen_utc   TEXT
);

CREATE INDEX idx_fpv_session ON fpv_detections(session_id);

-- LoRa/Meshtastic detections (from Meshtastic/Meshcore and UHF LoRa Leaves)
CREATE TABLE lora_detections (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id      TEXT NOT NULL REFERENCES sessions(session_id),
    timestamp_utc   TEXT NOT NULL,
    lat             REAL,
    lon             REAL,
    frequency_mhz   REAL NOT NULL,
    spreading_factor INTEGER,        -- SF6–SF12
    bandwidth_khz   REAL,            -- 125, 250, 500
    coding_rate     TEXT,            -- 4/5, 4/6, 4/7, 4/8
    rssi_dbm        INTEGER,
    snr_db          REAL,
    packet_length   INTEGER,
    protocol        TEXT,            -- meshtastic, meshcore, lorawan, elrs, crossfire, unknown
    header_hex      TEXT,            -- first N bytes of packet header (hex)
    node_id         TEXT,            -- Meshtastic node ID if decodable
    hop_count       INTEGER,         -- Meshtastic hop count if decodable
    channel_hash    TEXT             -- Meshtastic channel hash if decodable
);

CREATE INDEX idx_lora_session ON lora_detections(session_id);
CREATE INDEX idx_lora_protocol ON lora_detections(protocol);

-- Sub-GHz FSK detections (from 315/433/868/915 FSK Leaves)
CREATE TABLE subghz_detections (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id      TEXT NOT NULL REFERENCES sessions(session_id),
    timestamp_utc   TEXT NOT NULL,
    lat             REAL,
    lon             REAL,
    frequency_mhz   REAL NOT NULL,
    modulation      TEXT,            -- ASK, OOK, FSK, GFSK
    protocol        TEXT,            -- weather_station, tpms, garage_315, zwave, sidewalk, unknown
    raw_code        TEXT,            -- decoded bit pattern or hex
    repeat_count    INTEGER,         -- number of times code repeated (rolling code indicator)
    rssi_dbm        INTEGER,
    device_model    TEXT             -- if protocol decoder identifies specific model
);

CREATE INDEX idx_subghz_session ON subghz_detections(session_id);
```

**Design note:** Protocol-specific detection tables (fpv_detections, lora_detections, subghz_detections, radio_transcripts, aprs_beacons) are separate from the primary `detections` table because their schemas are fundamentally different. The primary `detections` table handles WiFi, BLE, BT Classic, 802.15.4, and HaLow — protocols where "identifier + name + RSSI + channel" is the universal data shape. LoRa, FPV, sub-GHz FSK, and radio transcripts have sufficiently different data shapes that forcing them into the same table would result in dozens of NULL columns per row.

The Root analyzer queries across all tables for map display (everything has lat/lon/timestamp), but each protocol family gets its own filter panel and detail views.

---

## 6. Session Metadata Updates (v2)

Added to v1 session metadata JSON:

```json
{
  "trunk": {
    "device": "jetson_orin_nano",
    "gps": "ublox_x20",
    "gps_update_rate_hz": 10,
    "storage": "nvme_256gb",
    "sdr_radios": [
      {"sdr_id": "sdr1", "device": "rtl-sdr-v4", "band": "VHF", "purpose": "p25_control"},
      {"sdr_id": "sdr2", "device": "rtl-sdr-v4", "band": "VHF", "purpose": "p25_voice"},
      {"sdr_id": "sdr3", "device": "rtl-sdr-v4", "band": "VHF", "purpose": "p25_voice"},
      {"sdr_id": "sdr4", "device": "rtl-sdr-v4", "band": "UHF", "purpose": "p25_control"},
      {"sdr_id": "sdr5", "device": "rtl-sdr-v4", "band": "UHF", "purpose": "p25_voice"},
      {"sdr_id": "sdr6", "device": "rtl-sdr-v4", "band": "UHF", "purpose": "p25_voice"}
    ],
    "radio_systems": [
      {
        "system_name": "County PD",
        "system_type": "P25_Phase1",
        "control_freq_mhz": 155.475,
        "talkgroup_map": {
          "101": "Dispatch",
          "102": "Patrol North",
          "103": "Patrol South"
        }
      }
    ]
  },
  "stm32_units": [
    {
      "unit_id": "stm32_rf",
      "device": "FK743M2-IIT6",
      "gps": "M10Q-5883",
      "branches": ["wifi24", "wifi5", "blebt", "dot154"]
    },
    {
      "unit_id": "stm32_subghz",
      "device": "FK743M2-IIT6",
      "gps": "M10_basic",
      "branches": ["meshtastic", "vhf_ism", "uhf_ism", "fpv_detect"]
    }
  ]
}
```

---

## 7. Report Additions (v2)

New report sections available:

| Section | Contents |
|---------|----------|
| UAS/FPV Activity | Map of detected FPV video transmitters (5.8 GHz) and LoRa C2 links. Per-detection frequency, signal strength, estimated location. Highlights rogue/unauthorized UAS activity. |
| Meshtastic/Mesh Network Activity | Map of detected Meshtastic and Meshcore nodes. Node density, channel usage distribution, hop count statistics. |
| Sub-GHz Device Inventory | Breakdown of 315/433/868/915 MHz detections by protocol. Garage door activity, weather station map, TPMS detections, Z-Wave device presence. |
| Radio Activity Summary | Heatmap of public safety radio activity along route. Talkgroup activity timeline. Optional: selected transcript excerpts (user-curated, not automatic — avoid bulk transcript inclusion for privacy/legal reasons). |
| APRS Coverage (future) | Map of APRS beacons received. Digipeater coverage assessment. |

---

## 8. Hardware Cost Estimate (Full Build)

| Category | Items | Est. Cost |
|----------|-------|-----------|
| **Trunk** | Jetson Orin Nano 8GB | $250 |
| | u-blox X20 or F9P GPS | $80–200 |
| | Powered USB hub (10-port) | $25 |
| | NVMe SSD 256GB | $30 |
| | IMU + Baro + power regulation | $25 |
| **STM32 ×2** | FK743M2-IIT6 boards | $50 |
| | M10Q-5883 + M10 basic GPS | $40 |
| | SD cards ×2 | $15 |
| **Branch Controllers** | RP2040 ×8 | $32 |
| | BN-220 GPS ×8 | $80 |
| | SN74LVC244AN buffers ×8 | $8 |
| **2.4 GHz WiFi** | ESP8266 ×4 (on hand) | $0 |
| **5 GHz WiFi** | XIAO ESP32-C5 ×3 | $21 |
| **BLE/BT** | ESP32-S3 ×2 | $14 |
| **802.15.4** | ESP32-H2 ×3 | $18 |
| **Meshtastic/Meshcore** | Heltec LoRa 32 V3 ×5 | $60 |
| **VHF ISM** | Arduino Nano ×6 + FSK RX ×6 | $30 |
| **UHF ISM** | HT-HC33 + Arduino Nano ×5 + SX1262 ×3 + FSK RX ×2 | $75 |
| **FPV Detection** | Arduino Nano ×2 + RX5808 ×2 + SX1262 + SX1280 | $50 |
| **SDR** | RTL-SDR Blog V4 ×6 | $180 |
| | VHF/UHF antennas | $40 |
| **Power** | Buck converters, LDOs, connectors, wiring | $40 |
| **Enclosure** | Project box, mounting hardware, antenna connectors | $50 |
| | | |
| **TOTAL** | | **~$1,200–1,350** |

Phased: Start with STM32 #1 + 2.4 GHz WiFi Branch only (~$200 including GPS and power). Add Branches incrementally.

---

## 9. Development Phases (Software-First)

### Phase 0 — Schema & Infrastructure
- Finalize all schemas (this document)
- Build SQLite database layer
- Build CSV import pipeline
- Stand up basic Flask/FastAPI backend + React frontend scaffold

### Phase 1 — Core Analyzer (2.4 GHz WiFi only)
- Map view, Table view, Device Detail
- Filters (protocol, channel, encryption, classification)
- OUI database, device signatures
- Static/mobile classification (DBSCAN)
- PDF report generator (basic sections)

### Phase 2 — Multi-Protocol
- Add 5 GHz WiFi, BLE/BT, 802.15.4 data support
- Protocol-specific filter panels
- Surveillance device detection (Flock, Raven, Ring, etc.)
- WiGLE export

### Phase 3 — GPS & Environment
- GPS Health view
- Jamming/interference detection
- Antenna orientation metadata
- Environment data overlay

### Phase 4 — Sub-GHz & LoRa
- LoRa detection table + views
- Sub-GHz FSK detection table + views
- Meshtastic/Meshcore node mapping
- FPV detection table + views

### Phase 5 — POL & IoT Audit
- POL whitelist/blacklist
- Cross-session device tracking
- IoT Audit scope selector + PDF generation
- Full report template system

### Phase 6 — SDR & Voice
- Radio transcript table + views
- trunk-recorder integration
- Whisper pipeline
- Radio activity heatmap

### Phase 7 — Live Operation
- Telemetry ingest (serial → WebSocket → map)
- Live detection plotting
- Surveillance alert display
- Session recording from telemetry

### Phase 8 — WIDS & Advanced
- Deauth/disassoc event display
- Rogue AP detection
- Evil twin detection
- UAS detection correlation (LoRa C2 + FPV video = probable drone)

### Hardware Build Sequence
(Follows software phases — build the Branch when its software support is ready)

1. 2.4 GHz WiFi Branch + STM32 #1 (supports Phase 1)
2. 5 GHz WiFi Branch (supports Phase 2)
3. BLE/BT Branch (supports Phase 2)
4. 802.15.4 Branch (supports Phase 2)
5. Meshtastic Branch + STM32 #2 (supports Phase 4)
6. VHF ISM Branch (supports Phase 4)
7. UHF ISM Branch (supports Phase 4)
8. FPV Detection Branch (supports Phase 4)
9. Jetson Trunk + SDR radios (supports Phase 6)
