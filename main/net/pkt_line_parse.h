#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Pure parsing of one "PKT:<bssid_hex12>,<ssid>,<rssi>,<channel>[,<sec>]"
// wire line (no FreeRTOS/UART dependency) into its fields. `out_ssid`/
// `out_sec` are filled truncated-and-NUL-terminated to their given
// capacities; `out_sec` defaults to "?" if the line has no 5th field
// (backward-compatible with the 4-field format some older C6 builds send).
// Returns false (leaving all outputs unset) if the line is malformed --
// missing the "PKT:" prefix, a non-hex BSSID, or fewer than 4 comma-
// separated fields.
//
// Not used by any current production caller. This parsed the old two-chip
// design's UART wire format; the standalone build's Wi-Fi Monitor gets AP
// data straight from the promiscuous-mode callback instead
// (c6_link.c's monitor_rx_cb() parses 802.11 beacon/probe-response frames
// directly -- no wire protocol, no "PKT:" lines) -- see KNOWN_ISSUES.md
// Round 27. Kept only because tests/test_wifi_pkt_parse.c host-tests it
// directly (#include "../main/net/pkt_line_parse.c") and it's a small,
// harmless, still-correct parser -- not built into the firmware image
// (main/CMakeLists.txt's SRCS list omits this .c file). Safe to delete
// along with its test if this wire format is never reintroduced.
bool pkt_line_parse(const char *line, uint8_t out_bssid[6],
                     char *out_ssid, size_t ssid_cap,
                     int *out_rssi, int *out_channel,
                     char *out_sec, size_t sec_cap);
