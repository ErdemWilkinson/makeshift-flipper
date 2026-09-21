# Phase 1 scope: display + buttons + menu only. Files for later phases are
# listed commented-out below, in the order the migration plan's Section 7
# phases introduce them -- uncomment each as its phase lands (and add the
# matching source file) rather than leaving a temporary local edit.

add_executable(makeshift_flipper_pico
    main.c
    ui/menu.c
    ui/display.c
    ui/font8x16_basic.c
    ui/text_entry.c
    input/buttons.c

    # Phase 2 -- vibration + diag/NVS foundation:
    # feedback/vibration.c
    # platform/nvs_compat.c
    # diag/diag.c

    # Phase 3 -- C6 link:
    # net/c6_link.c
    # net/json_escape.c

    # Phase 4 -- RFID:
    # rfid/rc522.c
    # rfid/rdm6300.c
    # rfid/rfid_library.c

    # Phase 6 -- IR (ir_direction.c intentionally omitted -- stays disabled/out of scope):
    # ir/ir_nec.c
    # ir/ir_driver.c
    # ir/ir_library.c
    # ir/ir_nec_pio_glue.c
)

# Phase 6 adds:
# pico_generate_pio_header(makeshift_flipper_pico ${CMAKE_CURRENT_LIST_DIR}/ir/ir_nec.pio)

target_include_directories(makeshift_flipper_pico PRIVATE
    .
    ui
    input
    ir
    rfid
    feedback
    net
    diag
    platform
)

target_link_libraries(makeshift_flipper_pico
    pico_stdlib
    hardware_spi
    hardware_gpio
    # Phase 3+ adds: hardware_uart
    # Phase 6 adds: hardware_pio, hardware_dma
    # Phase 2 adds: pico_rand, plus whichever LittleFS-for-Pico target is chosen
)

pico_enable_stdio_usb(makeshift_flipper_pico 1)
pico_enable_stdio_uart(makeshift_flipper_pico 0)  # UART0 is reserved for the C6 link (Phase 3) -- must NOT also be stdio

pico_add_extra_outputs(makeshift_flipper_pico)  # generates .uf2 for drag-and-drop flashing
