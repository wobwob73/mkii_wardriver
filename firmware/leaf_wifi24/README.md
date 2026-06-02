# `leaf_wifi24` — 2.4 GHz WiFi Branch Leaf Firmware

Single binary for all WiFi 2.4 GHz Leaf roles. Identity is adopted from the first `$CF` received on the Branch Controller UART link. Implements `wifi24_leaf_protocol_v1_1.md` with the v1.2 and **v1.3 (Scan-Hop)** amendments applied (encryption enum 0–10 including OWE and WPA3-Enterprise, passive scan timing, WIDS ring drop-newest, and the Scan-Hop role below).

## Target

- **Board:** Seeed XIAO ESP32-C3 (default), or any ESP32-C3 module with `Serial` mapped to UART0.
- **Framework:** Arduino via PlatformIO.
- **Wire:** 230 400 8N1 to the RP2040 Branch Controller's PIO UART. `ARDUINO_USB_CDC_ON_BOOT=0` keeps `Serial` on hardware UART0 rather than USB-CDC.

## Roles

- `W1`, `W2`, `W3` — scan Leaves, park on channels 1 / 6 / 11. Passive scan with `~200 ms` per channel; emit `$AP` per detected AP and a closing `$BK`.
- `W4` — WIDS Leaf, promiscuous mode hopping through all 14 channels with a 100 ms dwell. Emits `$BC` (beacons, deduplicated within a cycle), `$DE` (deauth/disassoc), `$PR` (probe requests).
- `WH1` — **Scan-Hop Leaf (`mode=2`, v1.3)**. A single passive-scan Leaf that round-robins a channel set instead of parking. Default hop set is `1057` (1/6/11) at 200 ms/ch (`$CH` overrides the mask, e.g. `2047` for all of 1–11); emits the **same** records as Scan (`$AP`, `$BK`, `$HB`) but with **one `$BK` per full sweep** (count/duration aggregated over the set). Added for the light-duty single-box aggregator (`agg_lite`), which covers 2.4 GHz with one Leaf. WIDS is not available in a single Scan-Hop Leaf (`lite_aggregator_v1_0.md` §6.1). Per v1.3 §5.2 the **standalone fallback (no `$CF` in 10 s) is now Scan-Hop** under `WH?` — overridden by the BC's explicit boot `$CF` in the full Branch, so W1–W4 behavior is unchanged in the field.

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
| `LEAF_VERSION` | `"1.3.0"` | Firmware build; protocol is `wifi24_leaf_protocol_v1_1` + v1.2 + v1.3 (Scan-Hop) amendments |
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
