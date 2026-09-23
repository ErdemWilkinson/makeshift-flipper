idf_component_register(
    SRCS
        "main.c"
        "ui/menu.c"
        "ui/display.c"
        "ui/font8x16_basic.c"
        "ui/text_entry.c"
        "input/buttons.c"
        "ir/ir_nec.c"
        "ir/ir_driver.c"
        "ir/ir_direction.c"
        "ir/ir_library.c"
        "rfid/rc522.c"
        "rfid/rdm6300.c"
        "rfid/rfid_library.c"
        "feedback/vibration.c"
        "diag/diag.c"
        # C6-standalone on-chip radio: net/c6_link.c calls
        # esp_wifi_* directly (init/scan/connect/monitor), net/radio_ble.c
        # drives NimBLE for BT Scan. Together they fill the same c6_link_*
        # API main.c already uses -- no UART, no second chip. See
        # MCU_ARCHITECTURE_DECISION.md.
        # json_escape.c / pkt_line_parse.c belonged to the old UART wire
        # protocol and stay out.
        "net/c6_link.c"
        "net/radio_ble.c"
    INCLUDE_DIRS
        "."
        "ui"
        "input"
        "ir"
        "rfid"
        "feedback"
        "net"
        "diag"
    REQUIRES
        driver
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_rmt
        esp_driver_spi
        esp_hw_support
        esp_lcd
        esp_timer
        log
        nvs_flash
        esp_driver_uart
        # Stage 2 radio: Wi-Fi station + scan + promiscuous monitor, and the
        # NimBLE BLE stack for BT Scan.
        esp_wifi
        esp_netif
        esp_event
        bt
)
