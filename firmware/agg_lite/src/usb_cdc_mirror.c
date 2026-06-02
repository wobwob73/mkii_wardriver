#include "usb_cdc_mirror.h"

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#include <stdio.h>

void usb_cdc_mirror_init(void) {
    /* CMake enables stdio over USB (and leaves UART0 for optional debug). */
    stdio_usb_init();
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
