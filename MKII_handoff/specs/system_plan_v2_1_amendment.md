# System Plan v2.1 — Amendment

**Date:** 2026-04-04
**Status:** Amendment to System Plan v2 (2026-04-03)
**Changes:** 1PPS timing replaces per-Branch GPS. Environmental Sensor Branch added. 2.4 GHz WiFi Leaves changed from ESP8266 to ESP32-C3.

This document specifies the exact changes to each section of the v2 system plan. Section numbers reference the v2 document.

---

## §1 System Hierarchy — Replace Entire Section

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
│         └── USB hub → all STM32s + SDRs
│
├── STM32 #1 (RF Collection) ──USB CDC──→ Trunk
│   ├── GPS: u-blox M10Q-5883 (I2C) — position + 1PPS out to Branches
│   ├── BRANCH: 2.4 GHz WiFi ───── RP2040 + 4× ESP32-C3
│   ├── BRANCH: 5 GHz WiFi ─────── RP2040 + 2–3× ESP32-C5
│   ├── BRANCH: BLE/BT ─────────── RP2040 + 2× ESP32-S3
│   ├── BRANCH: 802.15.4 ────────── RP2040 + 2–4× ESP32-H2
│   └── BRANCH: Env Sensors ─────── RP2040 + IMU + Mag + Baro + Thermo
│
├── STM32 #2 (Sub-GHz / FPV) ──USB CDC──→ Trunk
│   ├── GPS: u-blox M10 basic (I2C) — position + 1PPS out to Branches
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

---

## §1 GPS Tiering — Replace with 1PPS Timing Architecture

### Timing Architecture

Branch Controllers do not carry their own GPS modules. Timing is distributed from each STM32's GPS via a 1PPS signal and a `$TM` epoch message.

#### GPS Modules (Position + Time Source)

| Level | Device | Cost | Purpose |
|-------|--------|------|---------|
| STM32 #1 | Matek M10Q-5883 (u-blox M10 + compass) | ~$25 | Position for SD backup. 1PPS output to STM32 #1 Branches. Compass for heading. |
| STM32 #2 | u-blox M10 basic (SAM-M10Q or BN-880Q) | ~$15 | Position for SD backup. 1PPS output to STM32 #2 Branches. |
| Trunk (Jetson) | u-blox X20 or F9P | ~$80–200 | Reference-grade position. Multi-band, high-accuracy. GPS quality analysis, jamming detection. Authoritative position source. |
| Sensor Branch | Optional upgrade GPS (M10 or better) | ~$15–25 | Higher-grade position for sensor fusion if needed. Otherwise relies on STM32 GPS via 1PPS. |

#### 1PPS Distribution

Each STM32's GPS module provides a hardware 1PPS output — a rising edge at each UTC second boundary with ~30 ns accuracy. This signal fans out to every RP2040 Branch Controller attached to that STM32:

```
STM32 #1 GPS (M10Q-5883)
├── I2C → STM32 #1 (position + UBX time)
└── 1PPS ──┬── RP2040 Branch: WiFi 2.4 GHz (GPIO)
           ├── RP2040 Branch: WiFi 5 GHz (GPIO)
           ├── RP2040 Branch: BLE/BT (GPIO)
           ├── RP2040 Branch: 802.15.4 (GPIO)
           └── RP2040 Branch: Env Sensors (GPIO)

STM32 #2 GPS (M10 basic)
├── I2C → STM32 #2 (position + UBX time)
└── 1PPS ──┬── RP2040 Branch: Meshtastic (GPIO)
           ├── RP2040 Branch: VHF ISM (GPIO)
           ├── RP2040 Branch: UHF ISM (GPIO)
           └── RP2040 Branch: FPV Detection (GPIO)
```

The 1PPS output is a CMOS push-pull signal capable of driving 4–5 GPIO inputs without a buffer. If more Branches are added to a single STM32, an SN74LVC1G17 single Schmitt-trigger buffer can be used to boost fan-out.

#### $TM Epoch Message

After each PPS edge, the STM32 sends a `$TM` message on each Branch UART:

```
$TM,epoch_s,fix_ok*XX\n
```

| Field | Type | Description |
|---|---|---|
| epoch_s | int | UTC epoch seconds corresponding to the most recent 1PPS rising edge |
| fix_ok | int | 1 if GPS has valid fix, 0 if no fix |

The RP2040 associates the epoch with its local PPS capture timestamp. Sub-second interpolation uses the RP2040's crystal oscillator (±20 ppm drift = ±20 µs/s). At 120 mph, this is ~1 mm of positional uncertainty from timing — negligible compared to GPS position accuracy (2–5 m CEP).

#### PPS Loss Behavior

If the 1PPS signal stops (GPS fix lost, cable fault):
- The RP2040 detects loss after 2 seconds of no edge
- Falls back to free-running internal clock, incrementing epoch_s from the last known value
- All upstream records carry `time_flag=1` (degraded) until PPS resumes
- Free-running drift: ~20 µs/s → after 60 seconds without PPS, timing error is ~1.2 ms

#### Advantages Over Per-Branch GPS

| Factor | Per-Branch GPS (v2) | 1PPS (v2.1) |
|--------|---------------------|-------------|
| Hardware cost | 8× BN-220 = $80 | 1 wire per Branch = ~$0 |
| UART consumption | 1 PIO UART per Branch | 1 GPIO pin per Branch |
| Timing accuracy | Variable — NMEA sentence latency (50–80 ms) | ±20 µs (PPS edge + crystal interpolation) |
| Cold start | Each Branch GPS needs separate fix (30s–2 min) | One GPS fix propagates to all Branches |
| Failure modes | 8 GPS modules to fail independently | Single GPS per STM32 — simpler troubleshooting |
| Firmware complexity | NMEA parser per Branch | PPS ISR + $TM parser (simpler) |

---

## §3.1 STM32 #1 Branch Inventory — Replace Table

**UART budget: 6 usable (USART1, USART2, UART4, UART5, USART6, UART7)**
**Allocation: 5 Branches + GPS (I2C)**

| Branch | UART | Leaves / Sensors | 1PPS |
|--------|------|------------------|------|
| 2.4 GHz WiFi | USART1 | 3× ESP32-C3 (scan ch 1/6/11) + 1× ESP32-C3 (WIDS) | GPIO |
| 5 GHz WiFi | USART2 | 2–3× ESP32-C5 (UNII-1 / UNII-3 / optional UNII-2) | GPIO |
| BLE/BT | UART4 | ESP32-S3 (BLE scan) + ESP32-S3 (BT Classic inquiry) | GPIO |
| 802.15.4 | UART5 | 2–4× ESP32-H2 (Thread/Zigbee/Matter, 802.15.4 ch 11–26) | GPIO |
| Env Sensors | USART6 | IMU + Magnetometer + Barometer + Thermometer (see §3.8) | GPIO |

STM32 #1 GPS (M10Q-5883) connects via I2C. 1PPS output fans to all 5 Branch Controller GPIOs.

Note: The spare UART (USART6) from v2 is now allocated to the Sensor Branch. No spare UART remains on STM32 #1. If additional Branches are needed, an SPI-to-UART bridge (SC16IS752) or reassignment to STM32 #2 is required.

---

## §3.2 STM32 #2 Branch Inventory — Update Table Header

Remove "GPS" column from table. All Branches receive 1PPS from STM32 #2 GPS. No other changes to STM32 #2 Branch allocation.

| Branch | UART | Leaves | 1PPS |
|--------|------|--------|------|
| Meshtastic / Meshcore | USART1 | 4× Meshtastic + 1× Meshcore | GPIO |
| VHF ISM (315/433 MHz) | USART2 | 3× Arduino Nano @315 + 3× Arduino Nano @433 | GPIO |
| UHF ISM (868/915 MHz) | UART4 | See existing breakdown | GPIO |
| FPV Detection | UART5 | RX5808 scanner + LoRa C2 sniffers | GPIO |
| Spare | USART6 | Reserved | — |

STM32 #2 retains its spare UART.

---

## §3.8 Branch: Environmental Sensors (New Section)

### Purpose

High-rate environmental and inertial data capture, fused and decimated on a dedicated RP2040, delivered to the STM32 as a clean data stream. Isolates sensor fusion from the STM32's RF data aggregation responsibilities.

### Topology

```
RP2040 (Sensor Branch Controller)
├── I2C0 ── IMU (ICM-42688-P or BMI270)
├── I2C0 ── Magnetometer (LIS3MDL or MMC5603)
├── I2C0 ── Barometer (BMP390 or DPS310)
├── I2C0 ── Thermometer / Humidity (SHT40 or BME280)
├── 1PPS ── GPIO (from STM32 GPS)
└── UART ── STM32 USART6
```

All sensors share I2C0. The RP2040 polls at independent rates per sensor.

### Sensor Selection

| Sensor | Recommended Module | Interface | Sample Rate | Purpose |
|---|---|---|---|---|
| IMU (Accel + Gyro) | ICM-42688-P | I2C, addr 0x68 | 100 Hz | Vehicle heading, pitch, roll, vibration |
| Magnetometer | LIS3MDL | I2C, addr 0x1C | 10 Hz | Compass heading, magnetic anomaly detection |
| Barometer | BMP390 | I2C, addr 0x77 | 10 Hz | Altitude (barometric), pressure trend |
| Thermo / Humidity | SHT40 | I2C, addr 0x44 | 1 Hz | Ambient environment logging |

Total I2C bus load at these rates: ~40 transactions/second at 400 kHz. Negligible.

### Sensor Fusion

The RP2040's dual cores handle:

| Core | Responsibility |
|---|---|
| Core 0 | IMU read at 100 Hz. Madgwick or complementary filter for orientation (heading, pitch, roll) using gyro + accel + mag. Output: fused quaternion or Euler angles at 10 Hz (decimated from 100 Hz internal). |
| Core 1 | Barometer + thermometer polling. Altitude computation. Upstream message formatting. 1PPS time management. Transmit to STM32 via UART. |

### Upstream Messages (Sensor Branch → STM32)

Same NMEA-style framing as all other Branches.

#### `$EN` — Environment Record

Emitted at 10 Hz (every 100 ms).

```
$EN,SEN,timestamp,heading,pitch,roll,accel_x,accel_y,accel_z,mag_x,mag_y,mag_z,baro_hpa,alt_m,temp_c,humid_pct,time_flag*XX\n
```

| Field | Type | Description |
|---|---|---|
| branch_id | string | `SEN` |
| timestamp | float | UTC epoch, 6 decimal places (from 1PPS) |
| heading | float | Fused heading in degrees (0–359.99, magnetic north) |
| pitch | float | Degrees, nose-up positive |
| roll | float | Degrees, right-down positive |
| accel_x | float | m/s², body frame |
| accel_y | float | m/s², body frame |
| accel_z | float | m/s², body frame |
| mag_x | float | µT, raw magnetometer |
| mag_y | float | µT, raw magnetometer |
| mag_z | float | µT, raw magnetometer |
| baro_hpa | float | Barometric pressure in hPa |
| alt_m | float | Barometric altitude in meters (ISA model) |
| temp_c | float | Temperature in °C |
| humid_pct | float | Relative humidity % (0–100, or -1 if sensor absent) |
| time_flag | int | 0 = PPS-synced, 1 = degraded |

**Field count:** 17

At 10 Hz, this is ~10 messages/second × ~120 bytes ≈ 1.2 KB/s on the UART. Trivial at 230400 baud.

#### `$SB` — Sensor Branch Status

Emitted every 10 seconds.

```
$SB,SEN,uptime_s,time_valid,imu_ok,mag_ok,baro_ok,therm_ok,fusion_rate_hz,err_count*XX\n
```

| Field | Type | Description |
|---|---|---|
| branch_id | string | `SEN` |
| uptime_s | int | Seconds since boot |
| time_valid | int | 1 if PPS-synced |
| imu_ok | int | 1 if IMU responding |
| mag_ok | int | 1 if magnetometer responding |
| baro_ok | int | 1 if barometer responding |
| therm_ok | int | 1 if thermometer responding |
| fusion_rate_hz | int | Actual achieved fusion output rate |
| err_count | int | Cumulative I2C errors + sensor timeouts |

### Sensor Failure Handling

Each sensor is independently optional. If a sensor fails to respond on I2C:
- The corresponding fields in `$EN` are filled with `NaN` (the string `NaN`)
- The `$SB` status flags which sensors are offline
- The Branch Controller continues operating with remaining sensors
- I2C re-initialization is attempted every 30 seconds for failed sensors

If the IMU fails entirely, heading/pitch/roll are unavailable but barometer and thermometer data continue flowing. The STM32 and Trunk handle missing fields gracefully.

### Magnetic Calibration

The magnetometer requires hard-iron and soft-iron calibration for accurate heading. Two approaches:

1. **Factory calibration:** Pre-compute calibration offsets during bench setup. Store as compiled constants. Requires recalibration if the sensor's mounting position changes relative to ferrous materials.

2. **Runtime autocalibration:** Accumulate mag readings over a full rotation (during a drive), fit an ellipsoid, compute offsets dynamically. More robust but adds complexity. Defer to Phase 3 — use uncalibrated mag heading with a "cal_status" flag in `$EN` for now.

### GPS on Sensor Branch

The Sensor Branch receives 1PPS from the STM32 GPS like all other Branches. It does not require its own GPS module in the baseline configuration.

**Optional upgrade:** If higher-rate position data is needed for sensor fusion (e.g., GPS-aided INS for dead reckoning during GPS dropouts in tunnels/garages), a dedicated GPS module can be added to the Sensor Branch's I2C bus. This is a Phase 3+ enhancement, not required for initial operation.

---

## §4.1 Trunk — Remove Sensor Hub Role

Remove this line from the Trunk table:

> | Sensor hub | IMU, compass, barometer via I2C (Jetson has I2C GPIO headers). Alternatively, sensors stay on STM32 #1 and data is forwarded. |

Environmental sensors are now on the Sensor Branch (STM32 #1), not the Trunk. The Trunk receives fused sensor data via the STM32 CDC stream.

---

## §4.2 STM32 Role — Add 1PPS Responsibility

Add to the STM32 responsibilities list:

Each STM32:
- Has its own GPS (I2C)
- **Distributes 1PPS from GPS to all attached Branch Controllers (GPIO fan-out)**
- **Sends `$TM` epoch message on each Branch UART after every PPS edge**
- Has its own SD card (SDMMC1, onboard slot)
- Reads its Branches via UART
- Timestamps detections using its local GPS
- Writes to local SD as backup
- Streams formatted detection records to Trunk via USB CDC
- Accepts commands from Trunk via USB CDC

---

## §5 Database Schema — Add Environment Table

```sql
-- Environment records (from Sensor Branch)
CREATE TABLE environment_records (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id      TEXT NOT NULL REFERENCES sessions(session_id),
    timestamp_utc   TEXT NOT NULL,
    lat             REAL,           -- from STM32 GPS (merged at STM32 level)
    lon             REAL,
    heading_deg     REAL,           -- fused magnetic heading
    pitch_deg       REAL,
    roll_deg        REAL,
    accel_x_ms2     REAL,           -- m/s², body frame
    accel_y_ms2     REAL,
    accel_z_ms2     REAL,
    mag_x_ut        REAL,           -- µT, raw
    mag_y_ut        REAL,
    mag_z_ut        REAL,
    baro_hpa        REAL,           -- barometric pressure
    baro_alt_m      REAL,           -- barometric altitude
    temp_c          REAL,
    humidity_pct    REAL,
    time_quality    INTEGER         -- 0 = PPS-synced, 1 = degraded
);

CREATE INDEX idx_env_session ON environment_records(session_id);
CREATE INDEX idx_env_timestamp ON environment_records(timestamp_utc);
```

**Design note:** Environment records arrive at 10 Hz — significantly higher rate than RF detections. For a 1-hour drive, this is 36,000 rows. The table is separate from `detections` because its schema is fundamentally different (no BSSID/SSID/RSSI) and the write rate is fixed rather than environment-dependent. The Root analyzer correlates environment records with RF detections by timestamp for overlay views (e.g., heading arrow on map, altitude profile alongside detection density).

---

## §6 Session Metadata — Update stm32_units

Replace the `stm32_units` block:

```json
{
  "stm32_units": [
    {
      "unit_id": "stm32_rf",
      "device": "FK743M2-IIT6",
      "gps": "M10Q-5883",
      "timing": "1pps_fan_out",
      "branches": ["wifi24", "wifi5", "blebt", "dot154", "env_sensors"]
    },
    {
      "unit_id": "stm32_subghz",
      "device": "FK743M2-IIT6",
      "gps": "M10_basic",
      "timing": "1pps_fan_out",
      "branches": ["meshtastic", "vhf_ism", "uhf_ism", "fpv_detect"]
    }
  ]
}
```

---

## §7 Report Additions — Add Environment Section

Add to report sections table:

| Section | Contents |
|---------|----------|
| Environment & Motion | Heading plot over route, pitch/roll timeline (road grade, banking), altitude profile (barometric), temperature/humidity log. Vibration analysis from accelerometer data (connection quality indicator). |

---

## §8 Hardware Cost Estimate — Revised Lines

**Remove:**

| ~~Branch Controllers~~ | ~~BN-220 GPS ×8~~ | ~~$80~~ |
| ~~2.4 GHz WiFi~~ | ~~ESP8266 ×4 (on hand)~~ | ~~$0~~ |

**Add / Replace:**

| Category | Items | Est. Cost |
|----------|-------|-----------|
| **2.4 GHz WiFi** | ESP32-C3 ×4 (XIAO or SuperMini) | $16 |
| **Env Sensors** | ICM-42688-P breakout | $8 |
| | LIS3MDL breakout | $6 |
| | BMP390 breakout | $5 |
| | SHT40 breakout | $4 |
| **Branch Controllers** | RP2040 ×9 (was ×8, +1 for Sensor Branch) | $36 |

**Net cost change:** Removed $80 (BN-220 ×8). Added $16 (ESP32-C3) + $23 (sensors) + $4 (extra RP2040). Net savings: ~$37.

**Revised total:** ~$1,160–1,310 (was ~$1,200–1,350).

---

## §9 Development Phases — Updates

### Phase 3 — GPS & Environment (Updated)

- GPS Health view
- Jamming/interference detection
- **Environment data overlay (heading, altitude, temperature on map)**
- **Sensor Branch integration and calibration**
- **Vibration/motion analysis from accelerometer data**
- Antenna orientation metadata

### Hardware Build Sequence (Updated)

1. 2.4 GHz WiFi Branch + STM32 #1 (supports Phase 1)
2. **Env Sensor Branch (supports Phase 3, but can be built early since it's on STM32 #1)**
3. 5 GHz WiFi Branch (supports Phase 2)
4. BLE/BT Branch (supports Phase 2)
5. 802.15.4 Branch (supports Phase 2)
6. Meshtastic Branch + STM32 #2 (supports Phase 4)
7. VHF ISM Branch (supports Phase 4)
8. UHF ISM Branch (supports Phase 4)
9. FPV Detection Branch (supports Phase 4)
10. Jetson Trunk + SDR radios (supports Phase 6)

---

## Summary of All Changes

| Section | Change Type | Description |
|---------|-------------|-------------|
| §1 Hierarchy | Replace | ESP8266→ESP32-C3, add Sensor Branch, add 1PPS notation |
| §1 GPS Tiering | Replace | Entire section replaced with 1PPS Timing Architecture |
| §3.1 STM32 #1 Branches | Replace table | Remove GPS column, add Sensor Branch on USART6, ESP32-C3 |
| §3.2 STM32 #2 Branches | Update table | Remove GPS column |
| §3.8 Sensor Branch | New section | Full Sensor Branch specification |
| §4.1 Trunk | Remove line | Sensor hub role removed (moved to Sensor Branch) |
| §4.2 STM32 Role | Add items | 1PPS distribution, $TM generation |
| §5 Database Schema | Add table | environment_records |
| §6 Session Metadata | Update JSON | Add timing field, env_sensors branch |
| §7 Report Additions | Add row | Environment & Motion section |
| §8 Cost Estimate | Revise lines | Remove BN-220, add ESP32-C3 + sensors, net -$37 |
| §9 Dev Phases | Update | Phase 3 scope, build sequence reorder |
