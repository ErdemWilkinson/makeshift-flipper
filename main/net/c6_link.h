#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "diag/diag.h"

// Compatibility API retained from the former two-chip, UART-linked
// architecture. In the standalone build these functions operate the
// ESP32-C6's on-chip Wi-Fi/BLE radios directly; there is no wire protocol
// or companion MCU.

#define C6_MAX_NETWORKS 16
#define C6_SSID_MAX_LEN 32

typedef struct {
    char ssid[C6_SSID_MAX_LEN + 1];
    int8_t rssi;
} c6_network_t;

// Initializes Wi-Fi and BLE independently. A radio initialization failure
// is logged and disables that radio without aborting local UI/RFID/IR boot.
void c6_link_init(void);

// Blocking Wi-Fi scan. Returns a result count, 0 for a genuine empty scan,
// or -1 for initialization, radio-state, allocation, or driver failure.
int c6_link_scan(c6_network_t *out_networks, int max_networks);

// Connects the station interface and waits up to ten seconds. Empty password
// is accepted for open networks; non-empty WPA passwords must be 8-63 bytes.
bool c6_link_connect(const char *ssid, const char *password);

// Reserved compatibility entry point; not implemented in the standalone
// build and currently always returns false.
bool c6_link_send(const char *ip, uint16_t port, const char *data);

// Captive-portal setup is not merged yet. The generated PIN length remains
// part of the UI contract, but this function currently returns false.
#define C6_SETUP_PIN_LEN 8
bool c6_link_setup(const char *pin);

// Local diagnostics work offline. Network upload is not merged yet and this
// compatibility function currently returns false.
#define C6_LOGSEND_TIMEOUT_MS (15 * 1000)
bool c6_link_send_error_log(const diag_entry_t *entries, int count);

#define C6_MONITOR_MAX_APS 32
#define C6_MONITOR_SSID_MAX_LEN 32

// Short manufacturer label derived from the BSSID's OUI (first 3 bytes),
// looked up in a small built-in table -- see c6_link.c's oui_vendor_lookup().
// Purely a local, static lookup against publicly-registered IEEE OUI
// prefixes; it identifies hardware, not a person, and nothing is sent
// anywhere. "?" when the OUI isn't in the table.
#define C6_VENDOR_MAX_LEN 8

typedef struct {
    uint8_t bssid[6];
    char ssid[C6_MONITOR_SSID_MAX_LEN + 1];
    int8_t rssi;
    uint8_t channel;
    char sec[8];
    char vendor[C6_VENDOR_MAX_LEN + 1];
} c6_monitor_ap_t;

// Passive beacon/probe-response monitor. It disconnects STA, channel-hops
// while active, and is mutually exclusive with normal scan/connect and BLE
// discovery. Stopping does not reconnect the previous station.
bool c6_link_monitor_start(void);
bool c6_link_monitor_stop(void);
int c6_link_monitor_poll(c6_monitor_ap_t *out_aps, int max_aps);

#define C6_BT_MAX_DEVICES 32
#define C6_BT_NAME_MAX_LEN 31

typedef struct {
    uint8_t addr[6];
    char name[C6_BT_NAME_MAX_LEN + 1];
    int8_t rssi;
} c6_bt_device_t;

// Passive BLE advertisement discovery. It never connects or accesses GATT
// and is mutually exclusive with Wi-Fi monitor mode in this firmware.
bool c6_link_bt_scan_start(void);
bool c6_link_bt_scan_stop(void);
int c6_link_bt_scan_poll(c6_bt_device_t *out_devices, int max_devices);
