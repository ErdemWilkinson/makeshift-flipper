#pragma once

#include <stdbool.h>

// Passive BLE scan: listens for advertising packets (no connection, no
// GATT access, nothing sent except the standard active-scan probe request
// NimBLE issues by default -- see bt_scan.c for why this uses a passive
// scan instead) and reports each device seen (address, name if
// advertised, RSSI) back to the P4 as unsolicited "BTDEV:" lines, same
// asynchronous-push shape as wifi_monitor.c's "PKT:" lines.
//
// The C6 only has a BLE radio (no classic BT/BR-EDR) -- this can see BLE
// peripherals (headphones, fitness trackers, smart-home gadgets, BLE-mode
// speakers) but not classic-only devices. Never connects to anything it
// sees; this is observation only, same scope as Wi-Fi Monitor.

// Initializes the NimBLE host/controller and the UART TX task. Call once
// at startup.
void bt_scan_init(void);

// Starts passive BLE scanning. Returns false if the NimBLE stack couldn't
// start scanning (rare -- a wrapped API failure).
bool bt_scan_start(void);

// Stops scanning. Safe to call even if scanning wasn't running.
bool bt_scan_stop(void);
