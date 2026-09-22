#pragma once

// Skeleton for a future P4-side Wi-Fi-scan-specific protocol client --
// the SCAN/SCANDONE request/response handling that main/net/c6_link.c's
// c6_link_scan()/c6_link_scan_impl() currently own directly. This file
// is intentionally empty of real declarations until it's actually split
// out of c6_link.c; would depend on c6_transport.h for the underlying
// UART send/receive rather than talking to the driver directly.
//
// If/when this split happens: move c6_link.c's c6_link_scan()/
// c6_link_scan_impl() (and the c6_network_t type, C6_MAX_NETWORKS,
// C6_SSID_MAX_LEN from c6_link.h) here, update main.c's two scan call
// sites (action_wifi_scan_test(), action_wifi_setup_manual()) to include
// this header instead, and add "net/wifi_scan_client.c" to
// main/CMakeLists.txt's SRCS list.
