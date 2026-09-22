#pragma once

// Skeleton for a future standalone Wi-Fi scan handler -- the "SCAN"
// command handling that wifi_commands.c's wifi_commands_scan() currently
// owns directly (esp_wifi_scan_start()/esp_wifi_scan_get_ap_records(),
// "NET:<ssid>,<rssi>"/"SCANDONE" replies). This file is intentionally
// empty of real declarations until it's actually split out of
// wifi_commands.c; would depend on protocol.h for writing replies rather
// than calling uart_link_write_line() directly, matching whatever the
// split settles on.
//
// If/when this split happens: move wifi_commands_scan() here, update
// main.c's command dispatch to call it, and add "wifi_scan.c" to
// c6-firmware/main/CMakeLists.txt's SRCS list.
