#pragma once

// Skeleton for a future standalone Wi-Fi scan result screen -- the
// display/render logic main.c's action_wifi_scan_test() currently owns
// directly (scanning-state, found-networks list, no-networks/error
// states). This file is intentionally empty of real declarations until
// it's actually split out of main.c; would depend on
// net/wifi_scan_client.h for the scan call itself rather than calling
// c6_link_scan() directly.
//
// If/when this split happens: move action_wifi_scan_test()'s rendering
// (and any state it needs) here as a menu_action_fn-compatible entry
// point, update main.c's menu item wiring to call it, and add
// "ui/wifi_scan_screen.c" to main/CMakeLists.txt's SRCS list.
