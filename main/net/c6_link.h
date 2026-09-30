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

// Snapshot of the current station association and local IPv4 configuration.
// This does not test Internet reachability or enumerate the router's clients.
typedef struct {
    char ssid[C6_SSID_MAX_LEN + 1];
    int8_t rssi;
    uint8_t channel;
    char ip[16];
    char gateway[16];
} c6_wifi_status_t;

// Returns true only while associated with an AP and holding an IPv4 address.
bool c6_link_get_wifi_status(c6_wifi_status_t *out_status);

// Local, password-protected SoftAP. Starting it disconnects STA; stopping it
// restores STA mode without reconnecting to the old network. There is no
// Internet uplink, HTTP server, file transfer, or captive portal.
#define C6_AP_PASSWORD_LEN 12
typedef struct {
    char ssid[C6_SSID_MAX_LEN + 1];
    char password[C6_AP_PASSWORD_LEN + 1];
    char ip[16];
    int client_count; // -1 if the driver could not provide a fresh count
} c6_ap_status_t;

bool c6_link_ap_start(void);
bool c6_link_ap_stop(void);
bool c6_link_ap_is_running(void);
bool c6_link_ap_get_status(c6_ap_status_t *out_status);

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
    uint16_t seen_seq; // bumped on every received beacon; a change = fresh rssi
} c6_monitor_ap_t;

// Passive beacon/probe-response monitor. It disconnects STA, channel-hops
// while active, and is mutually exclusive with SoftAP, normal scan/connect
// and BLE discovery. Stopping does not reconnect the previous station.
bool c6_link_monitor_start(void);
bool c6_link_monitor_stop(void);
int c6_link_monitor_poll(c6_monitor_ap_t *out_aps, int max_aps);

// Restricts channel hopping to the channels set in `mask` (bit N = channel N,
// 1..13), so APs of interest are revisited more often. 0 hops all channels.
// Reset to 0 by c6_link_monitor_start().
void c6_link_monitor_set_hop_mask(uint16_t mask);

// Per-channel occupancy derived from the live monitor AP list: how many
// distinct APs are currently seen on each 2.4GHz channel, and the strongest
// RSSI among them. Indices 1..13 are valid (index 0 unused); channel 0 in an
// AP entry, if it ever occurs, is ignored. Purely a local visualization of
// the same passive beacon list c6_link_monitor_poll() returns -- no extra
// radio activity. Returns false if the monitor isn't running.
#define C6_CHANNEL_COUNT 13
typedef struct {
    uint8_t ap_count[C6_CHANNEL_COUNT + 1];   // [1..13], APs seen on that channel
    int8_t  best_rssi[C6_CHANNEL_COUNT + 1];  // [1..13], strongest RSSI, 0 if none
} c6_channel_stats_t;
bool c6_link_monitor_channel_stats(c6_channel_stats_t *out_stats);

// The 2.4GHz channel the hopping monitor is currently parked on (1..13), or 0
// when the monitor isn't running. Cosmetic, for the monitor screens' status line.
int c6_link_monitor_current_channel(void);

// Cumulative 802.11 frame-type tally for the current monitor session, counted
// in the promiscuous callback across every channel the hopper visits. Passive:
// it only classifies frames the radio already receives, transmits nothing, and
// keeps no addresses or payloads -- just counters. Reset on each
// c6_link_monitor_start(). Returns false if the monitor isn't running.
typedef struct {
    uint32_t mgmt_beacon;      // beacon frames
    uint32_t mgmt_probe_req;   // probe requests (client looking for a network)
    uint32_t mgmt_probe_resp;  // probe responses
    uint32_t mgmt_other;       // other management frames (auth/assoc/deauth/...)
    uint32_t data;             // data frames
    uint32_t ctrl;             // control frames (ACK/RTS/CTS/...)
    uint32_t total;            // sum of all frames classified
} c6_frame_stats_t;
bool c6_link_monitor_frame_stats(c6_frame_stats_t *out_stats);

// Passive probe-request collection. While the monitor runs, nearby client
// devices broadcast probe requests naming networks they are looking for; this
// gathers the distinct non-empty SSIDs seen (deduplicated), with a sighting
// count each. Strictly receive-only: it reads names devices broadcast on their
// own and stores no device addresses. Reset on each c6_link_monitor_start().
#define C6_PROBE_MAX 24
typedef struct {
    char ssid[C6_MONITOR_SSID_MAX_LEN + 1];
    uint16_t count; // times this SSID was seen in a probe request
} c6_probe_ssid_t;
// Snapshots up to max_entries probe SSIDs into out; returns the count copied,
// or 0 (also when the monitor isn't running).
int c6_link_monitor_probe_poll(c6_probe_ssid_t *out, int max_entries);

// Per-device probe view (for the combined radar): unlike the SSID-only list
// above, this keys on the *source MAC* of each probe request and keeps its
// RSSI, so the radar can place probing devices by direction/strength and
// correlate them with BLE devices at the same spot. Deduplicated by MAC.
// Still strictly receive-only -- it reads the source address and signal that
// every probe request already carries over the air; it transmits nothing.
#define C6_PROBE_DEV_MAX 24
typedef struct {
    uint8_t mac[6];
    int8_t rssi;                          // latest RSSI for this device
    char last_ssid[C6_MONITOR_SSID_MAX_LEN + 1]; // most recent network it asked for ("" if wildcard)
    uint16_t count;                       // probe requests seen from this MAC
    uint16_t seen_seq;                    // bumped each sighting; a change = fresh RSSI
} c6_probe_dev_t;
// Snapshots up to max_entries probing devices into out; returns the count
// copied, or 0 (also when the monitor isn't running).
int c6_link_monitor_probe_dev_poll(c6_probe_dev_t *out, int max_entries);

#define C6_BT_MAX_DEVICES 32
#define C6_BT_NAME_MAX_LEN 31

// Recognized beacon advertisement formats, decoded passively from the
// advertisement payload the device already broadcasts. Purely local parsing;
// nothing is transmitted and no connection is made.
typedef enum {
    C6_BEACON_NONE = 0,   // no recognized beacon frame in the advertisement
    C6_BEACON_IBEACON,    // Apple iBeacon (manufacturer data, Apple + type 0x02)
    C6_BEACON_EDDYSTONE,  // Google Eddystone (service data, UUID 0xFEAA)
} c6_beacon_type_t;

#define C6_BEACON_INFO_MAX_LEN 40

// Short guess at what a BLE device is, derived passively from advertised
// service UUIDs, the appearance field, and the manufacturer-data company id.
// A hint for the UI, not a definitive identification.
#define C6_BT_KIND_MAX_LEN 10

typedef struct {
    uint8_t addr[6];
    char name[C6_BT_NAME_MAX_LEN + 1];
    int8_t rssi;
    // Decoded beacon summary. type is C6_BEACON_NONE when the advertisement
    // carried no recognized beacon frame; info is a short human-readable
    // detail line (e.g. iBeacon UUID tail + major/minor, or Eddystone frame
    // kind) and is "" when type is C6_BEACON_NONE.
    c6_beacon_type_t beacon_type;
    char beacon_info[C6_BEACON_INFO_MAX_LEN + 1];
    // Best-effort device-kind label (e.g. "Kulaklik", "Saat", "Apple",
    // "Beacon"), or "" if nothing recognizable was advertised.
    char kind[C6_BT_KIND_MAX_LEN + 1];
    uint16_t seen_seq; // bumped on every received advertisement; a change = fresh rssi
    // --- Analyzer fields (pentest/privacy view) ---
    // BLE address type as reported by the controller: 0/1 = public/static
    // (potentially trackable across time), 2/3 = resolvable/non-resolvable
    // random (privacy-preserving, rotates). Passive: read from the header the
    // device already broadcasts.
    uint8_t addr_type;
    // Manufacturer "company id" from AD type 0xFF (little-endian first two
    // bytes), 0xFFFF if the advertisement carried no manufacturer-specific
    // data. Identifies the vendor (0x004C Apple, 0x0075 Samsung, ...).
    uint16_t company_id;
} c6_bt_device_t;

// Passive BLE advertisement discovery. It never connects or accesses GATT
// and is mutually exclusive with SoftAP and Wi-Fi monitor mode in this firmware.
bool c6_link_bt_scan_start(void);
bool c6_link_bt_scan_stop(void);
int c6_link_bt_scan_poll(c6_bt_device_t *out_devices, int max_devices);
