Vendored library: no-OS-FatFS-SD-SPI-RPi-Pico
Upstream: https://github.com/carlk3/no-OS-FatFS-SD-SPI-RPi-Pico
Pinned commit: 196016f525e5b9c161f2b965ddd3045a4ef87649 (branch master)
License: Apache-2.0 (see ./LICENSE)

Vendored VERBATIM — do not edit files under FatFs_SPI/ in place.
Only the FatFs_SPI/ library subtree is vendored (examples/tests omitted).
The application supplies hw_config (spi_get_*/sd_get_*) and the FatFs ffconf
is used as shipped. Integration glue lives OUTSIDE this tree, in
firmware/agg_lite/src/sd_hw_config.c and src/sd_spi_fatfs.c (the fatfs backend).
