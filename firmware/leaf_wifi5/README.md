# `leaf_wifi5` — 5 GHz WiFi Branch Leaf Firmware

Single binary for the 2–3 ESP32-C5 Leaves on the 5 GHz WiFi Branch. Identity adopted from the first `$CF` on the BC link. Implements `wifi5_branch_v1_0.md` §3–§8 (the Leaf-side half of the spec).

## Target

- **Board:** `esp32-c5-devkitc-1` (or Seeed XIAO ESP32-C5 once the board file is mainlined).
- **Flash:** the deployment target is the **Seeed XIAO ESP32-C5 (8 MB)**. The `esp32-c5-devkitc-1` board file assumes 4 MB, so `platformio.ini` overrides `board_build.flash_size`/`board_upload.flash_size = 8MB` (`flash_mode = qio`); without that the upper 4 MB is unpartitioned. Replace with a dedicated XIAO C5 board def when one is in the registry.
- **Framework:** Arduino via the **pioarduino** platform fork — `platform = https://github.com/pioarduino/platform-espressif32.git#55.03.38-1` (see `platformio.ini`). Mainline `espressif32` lags arduino-esp32 by months and does **not** yet carry the `esp32-c5-devkitc-1` board file or the arduino-esp32 v3.x with ESP32-C5 support; the pioarduino fork ships both. If a future mainline `espressif32` release adds C5 support, the same source builds against it with no code changes.
- **Wire:** 230 400 8N1 to the RP2040 Branch Controller's PIO UART. `ARDUINO_USB_CDC_ON_BOOT=0` so `Serial` is hardware UART0.

## Roles

| Leaf | Mode | Channel set | Notes |
|---|---|---|---|
| `W5_1` | scan | `csid=3` (UNII-1 + UNII-2A) | hops 36, 40, 44, 48, 52, 56, 60, 64 |
| `W5_2` | scan | `csid=6` (UNII-2C + UNII-3) | hops 100–144, 149–165 |
| `W5_3` | WIDS | `csid=7` (all 25 channels) | promiscuous, 100 ms dwell |

Scan dwell is **passive ~200 ms/channel** (no probe requests), matching the listen-only design philosophy from `wifi24_leaf_protocol_v1_2_amendment.md` §6.4. Per-cycle sweep times: W5_1 ≈ 1.6 s, W5_2 ≈ 3.4 s, W5_3 ≈ 2.5 s.

## Build / flash

```
pio run -e leaf_wifi5
pio run -e leaf_wifi5 -t upload
pio device monitor -b 230400      # observe traffic via a USB-serial probe;
                                  # Serial is the BC link, not USB-CDC
```

## File layout

```
leaf_wifi5/
├── platformio.ini
├── README.md
├── include/leaf_defs.h        constants, enums, ChannelSetId, 5 GHz channel table
└── src/
    ├── main.cpp
    ├── uart_proto.{h,cpp}     framing, checksum, hex encoding (lifted from leaf_wifi24)
    ├── config.{h,cpp}         identity + ChannelSetId + 32-bit channel mask
    ├── heartbeat.{h,cpp}      $HB emission + heap watchdog
    ├── cmd_handler.{h,cpp}    $CF (channel-set ID + dwell), $CH (lo/hi mask), $PG, $RB
    ├── wifi_scan.{h,cpp}      async passive scan, hopping across the assigned channel set
    ├── wids_monitor.{h,cpp}   promiscuous + ring + channel hop across $CH mask
    ├── frame_parser.{h,cpp}   802.11 mgmt header + beacon IE walker (band-agnostic)
    └── bssid_tracker.{h,cpp}  FNV-1a per-cycle BSSID seen set
```

## `$CF` payload shape

```
$CF,leaf_id,mode,channel_set_id,dwell_ms,reserved*XX
```

| Field | Description |
|---|---|
| `leaf_id` | `W5_1`, `W5_2`, or `W5_3` |
| `mode` | `0` = scan, `1` = WIDS |
| `channel_set_id` | `1`–`7` per `wifi5_branch_v1_0.md` §5.1 |
| `dwell_ms` | per-channel dwell time; `0` → default (200 scan / 100 WIDS) |
| `reserved` | send `0` |

## `$CH` payload shape (WIDS only)

```
$CH,leaf_id,bitmask_lo,bitmask_hi*XX
```

`mask = (bitmask_hi << 16) | bitmask_lo` covers the canonical 25-channel ordering documented in `leaf_defs.h::CHANNELS_5G[]`. The Leaf ignores `$CH` when in scan mode and acknowledges with `$HB` unchanged.

## Notes

- `esp_wifi_set_band(WIFI_BAND_5G)` is invoked once at boot in both `wifi_scan::init()` and `wids_monitor::start()`, guarded by `#ifdef LEAF_BAND_5G` (the build flag set in `platformio.ini`, always defined for this env). `WIFI_BAND_5G` is an esp-idf `wifi_band_t` enumerator (from `<esp_wifi_types.h>`), present in the arduino-esp32 v3.x / IDF 5.4 that the pinned pioarduino platform carries. An earlier revision guarded on `#ifdef WIFI_BAND_5G`, which — because the enumerator is not a preprocessor macro — was always false and silently compiled the band-select out, leaving the C5 in 2.4 GHz. If you retarget to an SDK without `esp_wifi_set_band`, drop `-DLEAF_BAND_5G` rather than relying on the guard.
- The encryption-mapping helper covers OWE and 192-bit WPA3-Enterprise where the SDK exposes them, falling back to the RSN AKM walker (vendored from `leaf_wifi24`) when the SDK constant isn't present.
- HE/EHT (WiFi 6 / 6E / 7) IE decoding is **out of scope for v1.0** — beacons are still detected; only the per-AP `enc` field is surfaced. A future v1.1 amendment may add a `phy_mode` field.
