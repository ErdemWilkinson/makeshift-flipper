# Hardware test matrix

A checklist for verifying this device on real hardware, module by module,
after any firmware change that touches pins, UART, the joystick, IR, or
RFID. Pairs with the pin plan in [README.md](README.md#pin-plan) and the
protocol description in
[c6-firmware/README.md](c6-firmware/README.md#protocol-p4--c6-uart-115200-8n1-line-terminated-with-n).

This repo has never been flash-tested on real hardware as of this writing
(see [KNOWN_ISSUES.md](KNOWN_ISSUES.md)) — both firmwares now build clean
under ESP-IDF v5.3.1, but nothing below has been run on an actual board.
Treat every row as unverified until checked off.

How to use this: flash both firmwares, work through each row, and record
pass/fail plus any deviation (wrong pin, wrong polarity, timing issue).
Update the pin plan or the relevant `.c` file's `#define`s if your wiring
differs — don't just work around it in your head.

## 1. Power-on and display

| Check | Expected | Pin(s) |
|---|---|---|
| Device boots without a crash loop | OLED shows the main menu | — |
| OLED renders text cleanly, no garbled/mirrored pixels | Menu items readable | SDA=GPIO7, SCL=GPIO8 (I2C) |
| Display contrast/orientation looks right for your specific panel | No upside-down or inverted text | — |

## 2. Joystick and buttons

| Check | Expected | Pin(s) |
|---|---|---|
| Stick left/right toggles between menu items horizontally (where applicable) | Cursor moves | VRx=GPIO3 (ADC1) |
| Stick up/down moves cursor vertically | Cursor moves | VRy=GPIO4 (ADC1) |
| Stick returns to center reliably (no drift triggering phantom moves when idle) | No unintended input at rest | GPIO3/GPIO4 |
| Center press (SW) selects/activates | Menu action fires | GPIO22 |
| Standalone BACK button returns to the previous menu | Menu pops back a level | GPIO23 |
| Text entry grid (`text_entry.c`, used by WiFi Setup Manual's password screen) navigates and appends chars correctly | Matches `tests/test_text_entry.c`'s host-tested logic | via same joystick pins |

Note: `buttons.c`'s comment documents that GPIO0-6 is the only ADC1-capable
range on the P4, already crowded by GPIO0/1/2 (IR) and GPIO7/8 (OLED) —
if you rewire the joystick, keep X/Y on ADC1-capable pins.

## 3. Vibration motor

| Check | Expected | Pin(s) |
|---|---|---|
| Motor pulses on the events `vibration.c` is wired to (check call sites — no dedicated menu action) | Physical buzz felt | GPIO20 (via transistor) |
| No motor buzz bleeding into I2C/SPI lines (check wiring layout, not just firmware) | Clean display/RFID reads while motor fires | — |

## 4. IR: transmit + direction-finding receive

| Check | Expected | Pin(s) |
|---|---|---|
| "IR Send Test" transmits a recognizable NEC frame | Verify with a second IR receiver, phone camera (IR LEDs show as faint purple on most phone cameras), or a known target device (e.g. TV power) | TX=GPIO2 (via transistor) |
| Primary IR receiver picks up an incoming NEC frame | Decoded frame shown/logged | RX=GPIO1 |
| "IR Direction Find" correctly identifies which of the 4 receivers saw a signal first/strongest | North/East/South/West reported correctly for a source at each compass point | North=GPIO5, East=GPIO6, South=GPIO14, West=GPIO15 |
| No cross-talk between the 4 direction receivers (one lighting up shouldn't fire all 4) | Only the receiver actually facing the source triggers | GPIO5/6/14/15 |

Note: GPIO5/GPIO6 are also in the ADC1-capable GPIO0-6 range `buttons.c`'s
comment flags as scarce — a documented tradeoff (see `ir_direction.c`'s
`GPIO_NORTH`/`GPIO_EAST` comment), not a bug, but worth re-confirming
nothing else on your board wants those two pins for analog input.

## 5. RFID/NFC: RC522 (13.56MHz)

| Check | Expected | Pin(s) |
|---|---|---|
| "Read 13.56MHz" detects a MIFARE Classic card in range | UID displayed | SCK=GPIO10, MOSI=GPIO11, MISO=GPIO12, CS=GPIO9, RST=GPIO13 |
| SPI wiring doesn't conflict with any other SPI peripheral on the board | Clean reads, no bus contention | GPIO9-13 |
| "Clone (13.56MHz)" successfully reads a source card's sector data | No `RC522_DUMP_NO_SECTORS` in Errors | same SPI pins |
| "Clone (13.56MHz)" successfully writes to a target (writable) card | Target card's UID/data matches source afterward, verify with "Read 13.56MHz" | same SPI pins |
| Clone failure path records a diag entry with a specific failure reason rather than silently doing nothing | Check "Errors" menu shows `RC522_CLONE_WRITE_FAILED` when write fails (e.g. read-only card) | — |
| 7-byte/10-byte UID cards are reported as unsupported, not misread as a shorter UID | "Errors" shows `RC522_SCAN_UNSUPPORTED_UID`, on-screen message reads "7/10-byte UID: N/A" | — |

## 6. RFID: RDM6300 (125kHz)

| Check | Expected | Pin(s) |
|---|---|---|
| "Read 125kHz" detects a 125kHz EM4100-family tag in range | Tag ID displayed | RX=GPIO17 (UART, receive-only module) |
| Read is reliable across multiple tag presentations, not just the first one after boot | Repeated reads work without a reboot | GPIO17 |

## 7. P4 <-> C6 UART link

| Check | Expected | Pin(s) |
|---|---|---|
| P4 boots and successfully talks to the C6 (any WiFi/BT action returns something other than a link-layer timeout) | No `C6_LINK_*_FAILED` in Errors purely from link setup | P4: TX=GPIO18, RX=GPIO19 |
| C6 side's UART pins actually match your wiring | `c6-firmware/main/uart_link.c`'s `UART_TX_GPIO`/`UART_RX_GPIO` (defaults GPIO6/7) match the physical wiring to the P4 | C6: see `uart_link.c` (board-dependent, not fixed like the P4 side) |
| Link survives a long-running unsolicited-push session (WiFi Monitor or BT Scan left running several minutes) without desync | PKT:/BTDEV: lines keep parsing correctly, no garbled lines | GPIO18/19 |

This is the pairing most likely to have a wiring mismatch since the C6
side's pins are explicitly "board-dependent" per the README, unlike every
P4-side pin above.

## 8. C6 WiFi

| Check | Expected | Pin(s)/Bus |
|---|---|---|
| "WiFi Scan Test" returns a real AP list | SSIDs match what's actually broadcasting nearby | C6 WiFi radio (no dedicated GPIO, on-chip) |
| "WiFi Setup" (SoftAP-based) completes and the device ends up connected to the chosen network | Verify via a subsequent WiFi Scan Test / Errors → Send round-trip | — |
| "WiFi Setup Manual" (on-device password entry) connects successfully | Same as above, using the joystick text-entry grid from section 2 | — |
| "WiFi Monitor" shows real nearby beacon/probe-response traffic, correctly channel-hopping 1-13 | AP list changes as you move / as nearby APs change channel | — |
| WiFi Monitor does not attempt to reconnect STA automatically after stopping (by design — see c6-firmware/README.md) | Confirm this is still true, not an accidental regression | — |

## 9. C6 Bluetooth (NimBLE, passive scan)

| Check | Expected | Pin(s)/Bus |
|---|---|---|
| "BT Scan" lists real nearby BLE advertisers | Names/addresses match known nearby devices (phone, earbuds, etc.) | C6 BLE radio (on-chip) |
| Scan is genuinely passive — does not itself become discoverable or connectable | Verify with a second phone/BLE scanner that this device doesn't show up as advertising | — |
| WiFi Monitor and BT Scan are confirmed mutually exclusive (can't run both at once, per the UART link's single-consumer design) | Attempting to start one while the other runs fails cleanly, not silently | — |

## 10. Errors → Send (optional, needs a PC on the same LAN)

| Check | Expected | Pin(s)/Bus |
|---|---|---|
| "Errors" menu shows a real accumulated history after triggering a few of the failure modes above | Ring buffer entries match what actually happened, newest-first | — |
| Pressing Send while `debug_server.py` is running on a reachable PC succeeds | `error_log.jsonl` on the PC gets the new entries | via C6 WiFi + existing STA connection |
| Send fails cleanly (not a hang) when the log server host/port in Kconfig is wrong or unreachable | On-device "Send failed" message, no freeze | — |

## Known gaps not covered above

- No physical hardware exists to run any of this against yet (see
  [KNOWN_ISSUES.md](KNOWN_ISSUES.md)) — this matrix is the checklist for
  whenever that changes, not a report of what's already been verified.
- Timing-sensitive things (IR NEC frame decode margins, joystick ADC
  debounce/hysteresis thresholds) can only really be tuned against real
  hardware; the host tests in `tests/` intentionally don't try to fake
  those.
- Flash/erase-cycle durability of the RC522 clone write path isn't
  something this matrix attempts to cover — treat repeated clone-testing
  on the same target card as consuming its write endurance.
