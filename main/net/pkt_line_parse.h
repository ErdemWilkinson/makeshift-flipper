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
// Pulled out of c6_link.c as its own module (same pattern as
// json_escape.c) so it can be host-tested without the UART/FreeRTOS
// plumbing the rest of c6_link.c depends on -- c6_link.c's
// handle_pkt_line() (production, feeds the live monitor AP table) and
// tests/test_wifi_pkt_parse.c (host test) both call this exact function.
// There is no separate copy of this parsing logic anywhere else, so a
// test pass here is a real guarantee about the production wire format.
bool pkt_line_parse(const char *line, uint8_t out_bssid[6],
                     char *out_ssid, size_t ssid_cap,
                     int *out_rssi, int *out_channel,
                     char *out_sec, size_t sec_cap);
