# third_party — vendored libraries for agg_lite

## `pico_fatfs_spi/` — FatFs-over-SPI microSD driver (VENDORED)

Backs durable microSD logging for `agg_lite` (`lite_aggregator_v1_0.md` §9,
the `[NEW vendored]` library; §14 item 5).

| | |
|---|---|
| Upstream | https://github.com/carlk3/no-OS-FatFS-SD-SPI-RPi-Pico |
| Pinned commit | `196016f525e5b9c161f2b965ddd3045a4ef87649` (branch `master`) |
| License | Apache-2.0 (`pico_fatfs_spi/LICENSE`) |
| Vendored scope | the `FatFs_SPI/` library subtree only (examples/tests omitted) |

**Vendored verbatim — do not edit files under `FatFs_SPI/` in place.** Provenance
is in `pico_fatfs_spi/VENDOR.md`. All integration glue lives OUTSIDE this tree:

- `agg_lite/src/sd_hw_config.c` — the app-supplied `spi_get_*` / `sd_get_*`
  hardware config (SPI0 pins per §1). The library's own `hw_config.c` is
  disabled upstream precisely so the application provides this.
- `agg_lite/src/sd_spi_fatfs.c` (`AGG_SD_BACKEND_FATFS` branch) — maps the
  `sd_spi_fatfs.h` interface onto FatFs (`f_mount`/`f_open`/`f_write`/`f_sync`/…).

The FatFs configuration (`FatFs_SPI/ff15/source/ffconf.h`) is used **as shipped**
— it already enables what the persistence path needs (`FF_FS_MINIMIZE=0` for
`f_mkdir`/`f_rename`, `FF_USE_LFN=3` for the 15-char `YYYYMMDD_HHMMSS` session
directories). No override was needed.

Selected with `-DAGG_SD_BACKEND=fatfs`; the default `stub` backend does not
compile or link any of this. See `firmware/agg_lite/README.md` for details.
