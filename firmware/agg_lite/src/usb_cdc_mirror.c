#include "usb_cdc_mirror.h"

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#include <stdio.h>

void usb_cdc_mirror_init(void) {
    /* USB stdio is initialized on core 0 in main() before the core1 launch,
       so TinyUSB is serviced by the core that owns the default alarm pool.
       Initializing it here (core 1) left the tud_task timer on core 0's pool
       while the servicing IRQ was enabled on core 1 — the device never
       enumerated. Nothing to do here. */
}

bool usb_cdc_mirror_connected(void) {
    return stdio_usb_connected();
}

void usb_cdc_mirror_write(const char *line, size_t len) {
    if (!line || len == 0) return;
    if (!stdio_usb_connected()) return;    /* Standalone, or host gone: drop */
    fwrite(line, 1, len, stdout);
    fflush(stdout);
}
