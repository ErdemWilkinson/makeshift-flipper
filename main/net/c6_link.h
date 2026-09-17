#pragma once

#include <stdbool.h>
#include <stdint.h>

// P4-side client for the line-based UART protocol spoken to the ESP32-C6
// companion radio (see c6-firmware/ for the other end). Every command is
// one newline-terminated ASCII line out, and one newline-terminated ASCII
// line back:
//
//   SCAN                        -> zero or more "NET:<ssid>,<rssi>",
//                                   terminated by "SCANDONE"
//   CONNECT:<ssid>,<password>   -> "OK" or "FAIL"
//   SEND:<ip>:<port>:<data>     -> "SENT" or "FAIL"

#define C6_MAX_NETWORKS 16
#define C6_SSID_MAX_LEN 32

typedef struct {
    char ssid[C6_SSID_MAX_LEN + 1];
    int8_t rssi;
} c6_network_t;

// Starts the UART link to the C6. Call once at startup.
void c6_link_init(void);

// Sends SCAN and blocks until SCANDONE or timeout. Fills `out_networks`
// (capacity `max_networks`) and returns how many were found. Returns -1 on
// timeout/link error.
int c6_link_scan(c6_network_t *out_networks, int max_networks);

// Sends CONNECT:<ssid>,<password> and blocks for the result.
bool c6_link_connect(const char *ssid, const char *password);

// Sends SEND:<ip>:<port>:<data> and blocks for the result.
bool c6_link_send(const char *ip, uint16_t port, const char *data);

// Sends SETUP: tells the C6 to open its web-based Wi-Fi setup AP
// ("MakeshiftFlipper-Setup") and a captive HTTP page at 192.168.4.1, then
// blocks (up to several minutes -- there's a human in the loop) until the
// C6 reports a successful station connection or its own setup timeout
// elapses. Returns true once connected.
bool c6_link_setup(void);
