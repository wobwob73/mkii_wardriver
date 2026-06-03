idf_component_register(
    SRCS
        "main.c"
        "uart_proto.c"
        "config.c"
        "heartbeat.c"
        "ble_scan.c"
    INCLUDE_DIRS "."
    REQUIRES
        bt
        driver
        esp_timer
        esp_system
        nvs_flash
)
