# third_party — vendored libraries for agg_lite

## `pico_fatfs_spi/` (NOT yet vendored)

`lite_aggregator_v1_0.md` §9 specifies a vendored FatFs-over-SPI library here
(e.g. carlk3's `no-OS-FatFS-SD-SPI-RPi-Pico`) to back durable microSD logging.

It is intentionally **absent** in this commit (§14 open item 5 — "SPI-FatFs
library selection + SD write-stall headroom" is implementation-phase work). The
default build links the documented placeholder backend in
`src/sd_spi_fatfs.c` instead, so `agg_lite.uf2` still compiles and the
ring/session logic runs. See `firmware/agg_lite/README.md` § "SD logging —
backend status" for the steps to drop the real library in and select it with
`-DAGG_SD_BACKEND=fatfs`.
