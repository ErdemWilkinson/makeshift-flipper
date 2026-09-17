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
```

The P4-side counterpart to this protocol lives in `../main/net/c6_link.c`.

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
