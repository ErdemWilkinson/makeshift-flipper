#pragma once

#include <stdbool.h>

// Web-based Wi-Fi setup: opens a temporary AP ("MakeshiftFlipper-Setup")
// and a tiny HTTP server on 192.168.4.1. A phone connects to that AP, opens
// a browser, sees a list of scanned networks, picks one, enters the
// password, submits -- this then tries to join that network as a station
// (dual AP+STA mode, same pattern home routers use for first-time setup).

// Minimum/maximum length of the setup AP's own password, generated
// per-session by the P4 side and passed in as `pin` below (see
// action_wifi_setup() in main/main.c) -- WPA2-PSK requires at least 8
// characters.
#define WIFI_SETUP_AP_PIN_LEN 8

// Starts the AP (secured with `pin` as its WPA2-PSK password, must be at
// least WIFI_SETUP_AP_PIN_LEN characters) + HTTP server, and blocks until
// either:
//  - the form was submitted and the resulting STA connection succeeded, or
//  - `timeout_ms` elapsed with no successful connection.
// Tears the AP and HTTP server down before returning either way.
// Returns true if a station connection was established.
bool wifi_setup_ap_run(const char *pin, int timeout_ms);
