#pragma once

#include <stdbool.h>

// Web-based Wi-Fi setup: opens a temporary AP ("MakeshiftFlipper-Setup",
// open/no password) and a tiny HTTP server on 192.168.4.1. A phone connects
// to that AP, opens a browser, sees a list of scanned networks, picks one,
// enters the password, submits -- this then tries to join that network as
// a station (dual AP+STA mode, same pattern home routers use for first-time
// setup).

// Starts the AP + HTTP server and blocks until either:
//  - the form was submitted and the resulting STA connection succeeded, or
//  - `timeout_ms` elapsed with no successful connection.
// Tears the AP and HTTP server down before returning either way.
// Returns true if a station connection was established.
bool wifi_setup_ap_run(int timeout_ms);
