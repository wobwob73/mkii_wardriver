# System Plan v2.2 — Amendment

**Date:** 2026-05-29
**Status:** Amendment to System Plan v2.1 (2026-04-04)
**Changes:** Environmental Sensor Branch sensor set updated. SHT40 dropped (subsumed by SCD41). SCD41 added (CO2 + temperature + relative humidity). SGP41 added (VOC + NOx gas indices). `$EN`, `$SB`, `environment_records`, and the hardware cost estimate are updated accordingly.

This document specifies the exact changes to v2.1. Section numbers reference the v2.1 amendment document where applicable, otherwise the v2 system plan. v2.1's sections that this document does not mention are unchanged.

Background: `HANDOFF.md` §9 item 2 noted that the v2.1 amendment text was stale on the sensor set — the current design dropped SHT40 (because the SCD41 already provides accurate humidity and temperature) and added SGP41 for volatile-organic-compound and NOx sensing. This amendment makes those changes formal in the spec.

---

## §3.8 Topology — Replace I2C Sensor List

```
RP2040 (Sensor Branch Controller)
├── I2C0 ── IMU                          (ICM-42688-P, addr 0x68)
├── I2C0 ── Magnetometer                 (LIS3MDL,     addr 0x1C)
├── I2C0 ── Barometer                    (BMP390,      addr 0x77)
├── I2C0 ── CO2 + Temp + Humidity        (SCD41,       addr 0x62)
├── I2C0 ── VOC + NOx                    (SGP41,       addr 0x59)
├── 1PPS ── GPIO (from STM32 GPS)
└── UART ── STM32 USART6
```

All five sensors share I2C0. There are no addressing conflicts. SCD41 supplies the temperature and relative-humidity values that SGP41's gas index algorithm needs for compensation — this is an internal data dependency handled by the Branch Controller firmware, not a spec change to other Branches.

---

## §3.8 Sensor Selection — Replace Table

| Sensor | Module | I2C Addr | Sample / Output Rate | Purpose |
|---|---|---|---|---|
| IMU (Accel + Gyro) | ICM-42688-P | 0x68 | 100 Hz internal, decimated to 10 Hz | Vehicle heading, pitch, roll, vibration |
| Magnetometer | LIS3MDL | 0x1C | 10 Hz | Compass heading, magnetic anomaly detection |
| Barometer | BMP390 | 0x77 | 10 Hz | Altitude (barometric), pressure trend |
| CO2 + Temp + Humidity | SCD41 | 0x62 | 0.2 Hz (5 s periodic) | Cabin CO2 (ppm), ambient temperature, relative humidity |
| VOC + NOx | SGP41 | 0x59 | ~1 Hz gas indices (post-algorithm) | VOC index (1–500), NOx index (1–500) |

Total I2C bus load at 400 kHz: ~40 transactions/second from the IMU/mag/baro tier plus ~1/s from SCD41 plus ~10/s raw + ~1/s indexed from SGP41. Comfortably under bus capacity.

**Why SCD41 (replacing SHT40):** the SCD41 provides ±0.8 °C / ±6 %RH accuracy alongside its CO2 measurement, which is sufficient for the cabin-environment use case and avoids carrying a second temperature/humidity sensor. The SHT40's only advantage was its faster output rate (1 Hz vs SCD41's 0.2 Hz), which is not material for environmental logging in a moving vehicle.

**Why SGP41:** indoor-air-quality VOC and NOx indices add a useful second-order metric (intrusion of exhaust, smoke, fuel vapors) that correlates with location for the analyzer's environment overlay. Sensirion publishes a reference Gas Index Algorithm (BSD-licensed) that turns the raw signals into a normalized 1–500 scale; that algorithm runs in firmware on the RP2040.

---

## §3.8 Sensor Fusion — Update Core Responsibility Table

| Core | Responsibility |
|---|---|
| Core 0 | IMU read at 100 Hz. Madgwick or complementary filter for orientation (heading, pitch, roll) using gyro + accel + mag. Output: fused quaternion or Euler angles at 10 Hz (decimated from 100 Hz internal). |
| Core 1 | Barometer polling at 10 Hz. SCD41 polling at 0.2 Hz (cached between reads). SGP41 raw measurement at ~1 Hz with on-RP2040 Sensirion Gas Index Algorithm producing VOC and NOx indices. Altitude computation. Upstream message formatting. 1PPS time management. Transmit to STM32 via UART. |

### SGP41 Conditioning & Compensation

- On boot the SGP41 requires a 10-second conditioning period (`sgp41_execute_conditioning` command) before measurements are valid. Until conditioning completes, `voc_index` and `nox_index` are reported as the sentinel value `-1`.
- After conditioning the firmware enters `sgp41_measure_raw_signals` mode, passing relative humidity and temperature compensation values pulled from the most recent SCD41 reading. If the SCD41 has not yet produced a fresh reading (first 5 s after boot or after an SCD41 failure), the firmware passes the SGP41 datasheet defaults (50 %RH, 25 °C).
- The Sensirion Gas Index Algorithm needs a learning window (typically minutes for usable output, hours for full convergence). The algorithm state is held across power cycles only if persisted to flash — for v2.2 we do **not** persist; the index starts from defaults on every boot, which is acceptable for in-drive use because relative excursions over a single session are the useful signal, not absolute values.

---

## §3.8 `$EN` — Replace Message Definition

Emitted at 10 Hz (every 100 ms).

```
$EN,SEN,timestamp,heading,pitch,roll,accel_x,accel_y,accel_z,mag_x,mag_y,mag_z,baro_hpa,alt_m,temp_c,humid_pct,co2_ppm,voc_index,nox_index,time_flag*XX\n
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
| temp_c | float | Temperature in °C (from SCD41; `NaN` if SCD41 absent) |
| humid_pct | float | Relative humidity % (0–100; `NaN` if SCD41 absent) |
| co2_ppm | int | CO2 in ppm (from SCD41; `-1` if SCD41 absent or no fresh reading yet) |
| voc_index | int | Sensirion VOC index 1–500 (100 = baseline; `-1` until conditioned or if SGP41 absent) |
| nox_index | int | Sensirion NOx index 1–500 (1 = baseline; `-1` until conditioned or if SGP41 absent) |
| time_flag | int | 0 = PPS-synced, 1 = degraded |

**Field count:** 20 (was 17).

Wire-bandwidth recalc: ~10 messages/s × ~135 bytes ≈ 1.35 KB/s on the UART at 230 400 baud. Still trivial.

**Caching rules baked into the firmware (informative, not normative):**
- The 0.2 Hz SCD41 values are held in `$EN` for up to 6 s after the last successful read. If no new SCD41 reading arrives within 6 s, the firmware reports `co2_ppm=-1`, `temp_c=NaN`, `humid_pct=NaN` and flags `scd_ok=0` in `$SB`.
- The gas indices are emitted at 10 Hz with the most recent algorithm output; the algorithm itself updates at ~1 Hz.

---

## §3.8 `$SB` — Replace Message Definition

Emitted every 10 seconds.

```
$SB,SEN,uptime_s,time_valid,imu_ok,mag_ok,baro_ok,scd_ok,sgp_ok,fusion_rate_hz,err_count*XX\n
```

| Field | Type | Description |
|---|---|---|
| branch_id | string | `SEN` |
| uptime_s | int | Seconds since boot |
| time_valid | int | 1 if PPS-synced |
| imu_ok | int | 1 if IMU responding |
| mag_ok | int | 1 if magnetometer responding |
| baro_ok | int | 1 if barometer responding |
| scd_ok | int | 1 if SCD41 responding with fresh readings |
| sgp_ok | int | 1 if SGP41 responding and conditioned |
| fusion_rate_hz | int | Actual achieved fusion output rate |
| err_count | int | Cumulative I2C errors + sensor timeouts |

Note: `therm_ok` from v2.1 is removed and replaced by `scd_ok` + `sgp_ok`. The total field count stays at 10.

---

## §3.8 Sensor Failure Handling — Replace Section

Each sensor is independently optional. If a sensor fails to respond on I2C:

- The corresponding fields in `$EN` are filled with `NaN` (for floats) or `-1` (for the integer ppm / index fields).
- The `$SB` status flags which sensors are offline.
- The Branch Controller continues operating with remaining sensors.
- I2C re-initialization is attempted every 30 seconds for failed sensors. On SGP41 re-init the 10-second conditioning step is repeated and `voc_index` / `nox_index` return `-1` for the conditioning window.

If the IMU fails entirely, heading/pitch/roll are unavailable but barometer, SCD41, and SGP41 data continue flowing.

If the SCD41 fails, the SGP41 algorithm falls back to default compensation values (50 %RH, 25 °C) and remains usable; the analyzer should treat VOC/NOx indices flagged in this condition as lower-confidence.

---

## §5 Database Schema — Replace `environment_records` DDL

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
    temp_c          REAL,           -- from SCD41
    humidity_pct    REAL,           -- from SCD41
    co2_ppm         INTEGER,        -- from SCD41 (NULL if absent or stale)
    voc_index       INTEGER,        -- Sensirion VOC index 1-500 (NULL if absent or unconditioned)
    nox_index       INTEGER,        -- Sensirion NOx index 1-500 (NULL if absent or unconditioned)
    time_quality    INTEGER         -- 0 = PPS-synced, 1 = degraded
);

CREATE INDEX idx_env_session ON environment_records(session_id);
CREATE INDEX idx_env_timestamp ON environment_records(timestamp_utc);
```

The STM32 ingest layer converts the `$EN` sentinels into SQL `NULL`:
- `NaN` (float fields) → `NULL`
- `-1` (`co2_ppm`, `voc_index`, `nox_index`) → `NULL`

This keeps `NULL` semantics consistent for the analyzer ("no reading") rather than encoding the sentinel everywhere.

---

## §7 Report Additions — Update Environment & Motion Section

Replace the v2.1 row:

| Section | Contents |
|---------|----------|
| Environment & Motion | Heading plot over route, pitch/roll timeline (road grade, banking), altitude profile (barometric), temperature/humidity log, **CO2 ppm timeline (cabin ventilation indicator)**, **VOC and NOx indices (intrusion of exhaust, smoke, fuel vapors plotted alongside route)**. Vibration analysis from accelerometer data (connection quality indicator). |

---

## §8 Hardware Cost Estimate — Revised Lines

**Remove from v2.1 §8 add list:**

| ~~Env Sensors~~ | ~~SHT40 breakout~~ | ~~$4~~ |

**Add / Replace:**

| Category | Items | Est. Cost |
|----------|-------|-----------|
| **Env Sensors** | SCD41 breakout (Adafruit / Sparkfun) | $50 |
| | SGP41 breakout (Adafruit / Sensirion) | $20 |

**Net cost change vs v2.1:** removed $4 (SHT40), added $70 (SCD41 + SGP41). Net increase: ~$66.

**Revised total:** ~$1,225–1,375 (was ~$1,160–1,310 in v2.1).

The SCD41 is the dominant cost item on the Sensor Branch by a wide margin; if budget pressure shows up later, a single-purpose SHT40 + a separate cheaper CO2 sensor is a fallback, but the integrated SCD41 keeps the sensor count and firmware complexity lower.

---

## §9 Development Phases — No Change

The build sequence and phase scopes from v2.1 §9 are unchanged. The Sensor Branch remains build-item #2 (after WiFi 2.4 Branch), and its software belongs to Phase 3 (GPS & Environment) of the analyzer roadmap.

---

## Summary of All Changes

| Section | Change Type | Description |
|---------|-------------|-------------|
| §3.8 Topology | Replace I2C list | SHT40 removed; SCD41 + SGP41 added |
| §3.8 Sensor Selection | Replace table | Sensor list updated; SCD41 at 0.2 Hz, SGP41 at ~1 Hz post-algorithm |
| §3.8 Sensor Fusion | Update table + new subsection | Core 1 picks up SCD41/SGP41; SGP41 conditioning & compensation rules added |
| §3.8 `$EN` | Replace | Adds `co2_ppm`, `voc_index`, `nox_index`; 17 fields → 20 fields |
| §3.8 `$SB` | Replace | `therm_ok` removed; `scd_ok` + `sgp_ok` added; still 10 fields |
| §3.8 Sensor Failure Handling | Replace | SGP41 fallback compensation, conditioning re-run on re-init |
| §5 Schema | Replace `environment_records` DDL | Adds `co2_ppm`, `voc_index`, `nox_index` columns |
| §7 Reports | Replace row | CO2/VOC/NOx added to Environment & Motion |
| §8 Cost | Revise lines | SHT40 −$4; SCD41 +$50; SGP41 +$20; net +$66 |

---

## Cross-Reference Resolution

This amendment resolves item 2 from `HANDOFF.md` §9 ("Env Sensor sensor set changed"). After this amendment is incorporated, the v2.1 §3.8 text on SHT40 is **superseded** and should no longer be relied on.

The document version ladder is now: `system_plan_v2.md` (2026-04-03) → `system_plan_v2_1_amendment.md` (2026-04-04) → `system_plan_v2_2_amendment.md` (2026-05-29). Read all three together; each subsequent amendment only restates the sections it changes.
