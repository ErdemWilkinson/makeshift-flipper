# C6 companion firmware

A separate ESP-IDF project running on the ESP32-C6. Interprets
line-based commands coming over UART from the P4 and carries out the
Wi-Fi operations. Has no dependency on the P4's own firmware — it's
built and flashed to the C6 entirely on its own.

## Protocol (P4 ↔ C6, UART, 115200 8N1, line-terminated with `\n`)

```
P4 -> C6: SCAN
C6 -> P4: NET:<ssid>,<rssi>   (0 or more lines)
C6 -> P4: SCANDONE

P4 -> C6: CONNECT:<ssid>,<password>
C6 -> P4: OK   or   FAIL

P4 -> C6: SEND:<ip>:<port>:<data>
C6 -> P4: SENT   or   FAIL

P4 -> C6: LOGSEND:<json-line>   (1 or more lines, one JSON object per device error)
P4 -> C6: LOGSENDDONE
C6 -> P4: SENT   or   FAIL

P4 -> C6: SETUP:<pin>   (<pin> is a fresh random WPA2-PSK password the P4
                          generated for this session, min 8 chars -- becomes
                          the setup AP's own password, see "Wi-Fi setup
                          (entering a password)" in ../README.md and
                          wifi_setup_ap.c)
C6 -> P4: OK   or   FAIL   (blocks up to several minutes -- there's a human
                             filling out a web form on the other end)

P4 -> C6: MONITOR
C6 -> P4: OK   or   FAIL
C6 -> P4: PKT:<bssid_hex12>,<ssid>,<rssi>,<channel>   (unsolicited, repeating,
                                                        until MONITORSTOP)

P4 -> C6: MONITORSTOP
C6 -> P4: OK   or   FAIL

P4 -> C6: BTSCAN
C6 -> P4: OK   or   FAIL
C6 -> P4: BTDEV:<addr_hex12>,<name>,<rssi>   (unsolicited, repeating,
                                               until BTSCANSTOP)

P4 -> C6: BTSCANSTOP
C6 -> P4: OK   or   FAIL
```

MONITOR/PKT/MONITORSTOP and BTSCAN/BTDEV/BTSCANSTOP are the exceptions to
"one line out, one (or a terminated stream of) lines back": once the
initial `OK` is sent, the C6 pushes `PKT:`/`BTDEV:` lines on its own,
asynchronously, for as long as that mode is running — see
`wifi_monitor.c`/`bt_scan.c` and `../main/net/c6_link.h`'s
`c6_link_monitor_start()`/`c6_link_bt_scan_start()` comments for how the
P4 side handles that. Only one of MONITOR/BTSCAN can be active at a time
(both hold the P4's UART link exclusively for their whole session).

The P4-side counterpart to this protocol lives in `../main/net/c6_link.c`.

## Error log upload ("Errors" → Send menu item) — optional, no AI/Ollama

The P4 keeps its own rolling error history entirely on-device
(`main/diag/diag.h`) and the "Errors" menu works fully offline with no PC
involved. This section only covers the *optional* "Send" step: uploading
that history to a small PC-side helper for safekeeping/inspection.

`LOGSEND:<json-line>` / `LOGSENDDONE` is handled by
`wifi_commands_log_line()`/`wifi_commands_log_flush()` in
`main/wifi_commands.c`, which batches the incoming lines and POSTs them
as one request to `c6-firmware/tools/debug_server.py` running on a PC on
the same Wi-Fi network the C6 is connected to (STA mode). That script
just appends the entries to `error_log.jsonl` and returns an
acknowledgement -- no AI, no Ollama, nothing beyond a plain HTTP POST and
a file write. This is plain, unauthenticated HTTP on your local network —
fine for a hobby LAN, not something to expose beyond it.

**One-time PC-side setup:**

1. Run the helper script on a PC on the same network (no extra Python
   packages needed, standard library only):
   ```
   python c6-firmware/tools/debug_server.py
   ```
   It listens on `0.0.0.0:8765` and appends each upload to
   `c6-firmware/tools/error_log.jsonl` (created on first use).
2. Find that PC's LAN IP (`ipconfig` / `ifconfig`) and run
   `idf.py menuconfig` in `c6-firmware/`, then set it under
   **"Makeshift Flipper C6 -- Error Log"** → `MAKESHIFT_LOG_SERVER_HOST`
   (and `MAKESHIFT_LOG_SERVER_PORT` if you changed `PORT` in the script).
3. Rebuild and reflash the C6 firmware after changing the config.

**Known limits:**

- The PC's IP is set at build time via Kconfig — if it changes (e.g. no
  DHCP reservation), Send will just fail with no more specific reason
  shown. A static DHCP lease on the PC avoids this.
- No auth on the log endpoint — anything on the same LAN segment could
  also reach it. Acceptable for a home network, not for anything more
  exposed. The uploaded history can reveal what the device has been used
  for (which cards were cloned, which networks were scanned, etc.), so
  treat `error_log.jsonl` accordingly.
- `debug_server.py` isn't a service — it has to be started manually and
  stays running in its own terminal (or you set it up as a background
  service/task yourself; not covered here).
- One request at a time, synchronous — same blocking pattern as
  `SETUP`/`CONNECT`.

## Pin plan

The P4 side of the breadboard report is fixed: P4 GPIO18(TX)→C6 RX, P4
GPIO19(RX)→C6 TX. **Which GPIOs you use on the C6 side depends on your
own board** — `UART_TX_GPIO`/`UART_RX_GPIO` at the top of
`main/uart_link.c` currently assume GPIO6/7; change them to match your
actual wiring.

## Building

```
idf.py set-target esp32c6
idf.py build
idf.py -p COMx flash monitor
```

## Limits / not yet implemented

- `SEND` opens and closes a new TCP connection every time (no
  persistent socket)
- Wi-Fi credentials (ssid/password) aren't stored persistently — every
  `CONNECT` reconnects using whatever the P4 sends that time
- Error reporting is just `FAIL` — the reason (wrong password, network
  not found, connection refused) isn't distinguished. Could be extended
  to something like `FAIL:<reason>` if needed
- No UDP support, TCP only
- Wi-Fi Monitor (`wifi_monitor.c`) is receive-only: it parses
  beacon/probe-response frames for SSID/BSSID/RSSI/channel, but never
  transmits a management frame of its own — no deauth, no injection, by
  design
- Wi-Fi Monitor doesn't reconnect STA automatically after
  `MONITORSTOP` — run `WiFi Setup`/`WiFi Setup Manual` again from the P4
  side if a connection is needed after monitoring
- BT Scan (`bt_scan.c`) is passive BLE advertisement scanning only — no
  connection, no GATT access, and it only ever listens (`passive = 1` in
  `ble_gap_disc_params`), so it never even sends the standard active-scan
  probe request. The C6 has no classic BT/BR-EDR radio, only BLE, so
  classic-only devices won't show up
- Wi-Fi Monitor and BT Scan can't run at the same time — not a radio
  conflict (BLE and Wi-Fi coexist fine on the C6), but both hold the
  shared UART link exclusively for their whole session, and the wire
  protocol has no way to multiplex two unsolicited-push streams at once
