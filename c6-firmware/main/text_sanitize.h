#pragma once

#include <stddef.h>
#include <stdint.h>

// Wire-format string sanitizing and BLE advertising-data parsing shared by
// wifi_monitor.c and bt_scan.c. Pulled out as its own module (rather than
// duplicated static functions in each) so it can be host-tested without
// pulling in FreeRTOS/esp_wifi/NimBLE.

// Replaces ',' / '\n' / '\r' with '_' in place. Both PKT (Wi-Fi Monitor)
// and BTDEV (BT Scan) lines are comma-separated and newline-terminated;
// SSIDs and BLE device names come from whatever's broadcasting nearby and
// aren't trustworthy input, so any of those three bytes must not reach the
// wire unescaped or they'd break the line format the P4 side parses.
void sanitize_wire_text(char *text);

// Pulls the Complete/Shortened Local Name AD structure out of a raw BLE
// advertising report, if present. BLE advertising data is a sequence of
// [length][type][data...] structures; type 0x09 is "Complete Local Name",
// 0x08 is "Shortened Local Name" -- either is good enough to show.
// Writes an empty string to out_name if no such structure is found.
// out_cap must be >= 1.
void extract_ble_name(const uint8_t *data, uint8_t len, char *out_name, size_t out_cap);
