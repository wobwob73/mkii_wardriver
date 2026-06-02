# `leaf_wifi24` — 2.4 GHz WiFi Branch Leaf Firmware

Single binary for all four WiFi 2.4 GHz Leaves (W1–W4). Identity is adopted from the first `$CF` received on the Branch Controller UART link. Implements `wifi24_leaf_protocol_v1_1.md` with the v1.2 amendment applied (encryption enum 0–10 including OWE and WPA3-Enterprise, passive scan timing, WIDS ring drop-newest).

## Target

- **Board:** Seeed XIAO ESP32-C3 (default), or any ESP32-C3 module with `Serial` mapped to UART0.
- **Framework:** Arduino via PlatformIO.
- **Wire:** 230 400 8N1 to the RP2040 Branch Controller's PIO UART. `ARDUINO_USB_CDC_ON_BOOT=0` keeps `Serial` on hardware UART0 rather than USB-CDC.

## Roles

- `W1`, `W2`, `W3` — scan Leaves, park on channels 1 / 6 / 11. Passive scan with `~200 ms` per channel; emit `$AP` per detected AP and a closing `$BK`.
- `W4` — WIDS Leaf, promiscuous mode hopping through all 14 channels with a 100 ms dwell. Emits `$BC` (beacons, deduplicated within a cycle), `$DE` (deauth/disassoc), `$PR` (probe requests).

## Build / flash

```
pio run -e leaf_wifi24                # build
pio run -e leaf_wifi24 -t upload      # flash via USB
pio device monitor -b 230400          # observe link traffic (USB-serial probe required;
                                      #   Serial is the BC link, not USB-CDC)
```

## File layout

```
leaf_wifi24/
├── platformio.ini
├── README.md
├── include/leaf_defs.h
└── src/
    ├── main.cpp
    ├── uart_proto.{h,cpp}    framing, checksum, hex enc/dec, line receiver
    ├── config.{h,cpp}        identity + mode state; default fallback
    ├── heartbeat.{h,cpp}     $HB emission, scan/error counters, heap watchdog
    ├── cmd_handler.{h,cpp}   $CF / $CH / $PG / $RB dispatch
    ├── wifi_scan.{h,cpp}     async passive scan (W1–W3)
    ├── wids_monitor.{h,cpp}  promiscuous + ring + channel hop (W4)
    ├── frame_parser.{h,cpp}  802.11 mgmt header + beacon IE walker
    └── bssid_tracker.{h,cpp} FNV-1a open-addressing seen set, 512 slots
```

## Build flags

| Flag | Default | Notes |
|---|---|---|
| `LEAF_VERSION` | `"1.2.2"` | Firmware build; protocol is `wifi24_leaf_protocol_v1_1` + v1.2 amendment |
| `MAX_LINE_LEN` | 200 | Max NMEA-style line length |
| `HB_INTERVAL_MS` | 10000 | `$HB` cadence |
| `CONFIG_TIMEOUT_MS` | 10000 | Standalone fallback timeout |
| `SCAN_FAIL_RESET_THRESHOLD` | 10 | Consecutive scan failures before WiFi stack reset |
| `SCAN_FAIL_REBOOT_THRESHOLD` | 50 | Consecutive scan failures before self-reboot |
| `HEAP_MIN_BYTES` | 16384 | Heap watchdog trip point |
| `WIDS_RING_SIZE` | 64 | Promisc callback → main-loop ring capacity |
| `WIDS_SEEN_BSSID_MAX` | 512 | Per-cycle BSSID dedup hash size |

## Notes

- This is a re-derivation of the v1.1 firmware described in `MKII_handoff/CODE_STATUS.md`. The original source was lost with the prior sandbox; this implementation is built directly from `specs/wifi24_leaf_protocol_v1_1.md` and the v1.2 amendment.
- The XIAO ESP32-C3 ships with the RF switch in chip-antenna mode. For external antenna use, set the U.FL solder bridge per the Seeed datasheet before deployment.
