# leaf_ble — BLE-only Leaf (ESP32-S3, NimBLE)

The BLE scanner Leaf from `blebt_branch_v1_0.md` §7, built here for the first
time so the light-duty aggregator (`agg_lite`) and the full BLE/BT Branch can
consume it. A NimBLE passive observer captures advertising PDUs across all three
primary channels on 1M + Coded PHYs and emits `$BL` / `$BX` / `$BK` / `$HB` on
the Branch-Controller (or lite-aggregator) UART link.

**Strict listen-only**: `passive=1` extended scan, no scan requests, no
connections, no pairing (HANDOFF §1).

BT Classic is a **separate binary** (`leaf_bt_classic`, NimBLE vs Bluedroid —
ESP-IDF can't host both on one S3) and is **not** built here; light-duty defers
it (`lite_aggregator_v1_0.md` §6.3).

## Files

| File | Role |
|---|---|
| `ble_defs.h` | constants + `BleAdvEvent` / `BleLeafConfig` |
| `uart_proto.{c,h}` | NMEA framing, XOR checksum, hex/MAC, line receiver (ESP-IDF UART driver) |
| `config.{c,h}` | identity from `$CF` (`BLE-1`), mode/phy/window params |
| `heartbeat.{c,h}` | `$HB` (scan_count = completed 1 s windows) |
| `ble_scan.{c,h}` | NimBLE host bring-up, `ble_gap_ext_disc`, per-event parse → queue |
| `main.c` | `app_main`: NVS, command handling, windowed dedup, `$BL`/`$BX`/`$BK` |

## Build

```sh
pio run -e leaf_ble        # platform=espressif32, framework=espidf, board esp32-s3-devkitc-1
```

`sdkconfig.defaults` enables NimBLE with extended advertising + observer role
(§7.2). CI: `.github/workflows/leaf_ble.yml`. As with `leaf_wifi5`, the ESP-IDF
framework + toolchain are fetched on first build, which the unrestricted CI
runner does but a TLS-intercepting sandbox may block.

## `$BX` size bounding

The binding line-length constraint is the **upstream** `$BX` the aggregator/BC
logs, which prepends `branch_id` + `timestamp` + `time_flag` (~49 B of fixed
overhead) to the three variable hex fields. So the leaf caps the **combined**
manufacturer-data + service-UUID-list + name-overflow hex to ≤140 chars —
manuf ≤30 B, svc ≤24 B truncated on **whole 2-byte UUID boundaries**, name ≤16 B
beyond the 16 already in `$BL` — which keeps the upstream `$BX` (≤189 B body)
inside `MAX_LINE_LEN` with a valid checksum. Any truncation (here or at parse-time
storage) raises the `$HB` `err_count` as the **truncation indicator**; the spec's
in-band "low bit" marker (`blebt §4.2`) is deferred to the analyzer contract
rather than invented unilaterally on the wire.

## Deviations / known limitations (documented, not silent)

- **Channel is reported as `0`.** NimBLE's host API does not surface the primary
  advertising channel (37/38/39) of each report, so `$BL.channel` is `0`. This
  deviates from `blebt_branch_v1_0.md` §16.1 step 4 (per-channel observation) and
  is an honest limitation of the host-level scan path. Recoverable only via
  controller HCI metadata; flagged for a later pass.
- **`$CF` `window_ms`/`interval_ms` are advisory.** The observer runs the fixed
  100%-duty extended scan from §7.3 (`itvl = window = 0x0010`); the params are
  accepted (and reflected in identity adoption) but do not retune the duty cycle.
- **PHY S=2 vs S=8 not distinguished.** Coded-PHY reports map to `phy=3`; the
  host does not expose the S-rate. (`phy` 1 = 1M, 3 = Coded.)
- **RPA/NRPA classification** is derived from the BD address MSB per the Core
  spec; RPAs are not resolved in firmware (analyzer-side task, §17 item 1).
- **BC-link UART pins are the XIAO ESP32-S3 header pads D0/D1** — `LEAF_UART_TX_PIN=1` (GPIO1/D0), `LEAF_UART_RX_PIN=2` (GPIO2/D1). GPIO17/18 are not broken out on the XIAO S3, so D0/D1 (exposed and free) are the chosen pins; still subject to final PCB layout.

These are correctness-of-reporting notes; the framing, dedup, and message
schemas are spec-conformant and exercised by CI compilation. On-hardware
verification of NimBLE Coded-PHY stability is `blebt_branch_v1_0.md` §17 item 5.
