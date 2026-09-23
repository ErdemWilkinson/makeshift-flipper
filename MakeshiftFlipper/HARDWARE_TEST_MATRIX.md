# Hardware test matrix

A checklist for verifying this device on real hardware, module by module,
after any firmware change that touches pins, the joystick, IR, RFID, or the
Wi-Fi/BLE radio. Pairs with the pin map in
[C6_STANDALONE_HARDWARE_PLAN.md](C6_STANDALONE_HARDWARE_PLAN.md).

The single-MCU ESP32-C6 firmware builds clean under ESP-IDF v5.3.1 and
produces a flashable image, but the hardware has not been assembled or
flashed. Treat every row below as unverified until checked off on your own
hardware. Follow the bring-up order in
[C6_STANDALONE_HARDWARE_PLAN.md](C6_STANDALONE_HARDWARE_PLAN.md) (display +
buttons first, then RFID, IR, and last the power/battery chain).

How to use this: flash the firmware (`idf.py flash`), work through each row,
and record pass/fail plus any deviation (wrong pin, wrong polarity, timing
issue). Update the pin map or the relevant `.c` file's `#define`s if your
wiring differs — don't just work around it in your head.

## 1. Power-on and display

| Check | Expected | Pin(s) |
|---|---|---|
| Device boots without a crash loop | LCD shows the main menu | — |
| LCD renders text and colors cleanly, no garbled/mirrored pixels | Menu items readable, correct colors (not swapped R/B) | SCK=GPIO7, MOSI=GPIO8, CS=GPIO14, DC=GPIO15, RST=GPIO6, BL=GPIO21 (SPI) |
| Colors are correct, not inverted (e.g. background isn't white when it should be black) | If colors look inverted, toggle `esp_lcd_panel_invert_color()`'s argument in `display_init()` -- this varies by panel batch | — |
| Display orientation looks right for your specific panel (not upside-down or mirrored) | If wrong, adjust `esp_lcd_panel_mirror()`/`esp_lcd_panel_swap_xy()` calls (not currently called -- add if needed) | — |
| Backlight turns on with the device (not separately wired to always-on power) | Screen is lit only when GPIO21 drives it high | GPIO21 |

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
range on the P4, already crowded by GPIO0/1/2 (IR) and GPIO7/8 (LCD SCK/MOSI) —
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
| "IR Learn" captures a remote button press and saves it under a chosen name | Entry appears in "IR Library" afterward, survives a reboot | RX=GPIO1 |
| "IR Library" replays (PRESS) a saved code | Target device reacts the same as to the original remote | TX=GPIO2 |
| "IR Library" delete (LEFT) removes an entry and persists the change | Entry gone from the list after a reboot | — |
| "IR Learn"/"IR Library" behave correctly at capacity (16 entries) | "Library full" shown, existing entries untouched, no crash | — |
| ⚠️ **"IR Direction Find" is currently non-functional on this build** — see Round 13 in [KNOWN_ISSUES.md](KNOWN_ISSUES.md): `ir_direction_init()` is no longer called from `app_main()` (the P4 has no spare RMT RX channel once the regular IR receiver/transmitter are running), so the screen waits forever. Do not test this row until that's redesigned. | — | North=GPIO5, East=GPIO6, South=GPIO14, West=GPIO15 |

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
| "Save 13.56MHz" saves a scanned UID under a chosen name (UID only, no sector data) | Entry appears in "RFID Library" marked 'H' (high frequency), survives a reboot | same SPI pins |

## 6. RFID: RDM6300 (125kHz)

| Check | Expected | Pin(s) |
|---|---|---|
| "Read 125kHz" detects a 125kHz EM4100-family tag in range | Tag ID displayed | RX=GPIO17 (UART, receive-only module) |
| Read is reliable across multiple tag presentations, not just the first one after boot | Repeated reads work without a reboot | GPIO17 |
| "Save 125kHz" saves a scanned tag ID under a chosen name | Entry appears in "RFID Library" marked 'L' (low frequency), survives a reboot | GPIO17 |
| "RFID Library" delete (LEFT) removes an entry (either kind) and persists the change | Entry gone from the list after a reboot | — |
| "Save 125kHz"/"Save 13.56MHz" behave correctly at capacity (16 entries total) | "Library full" shown, existing entries untouched, no crash | — |

## 7. On-chip radio bring-up (single MCU)

There is no UART link any more — the radio is on the same ESP32-C6 as the
UI. These rows check that the on-chip Wi-Fi/BLE actually initializes and
coexists with the display.

| Check | Expected | Notes |
|---|---|---|
| Wi-Fi + BLE both initialize at boot | No radio-init error in the log; a Wi-Fi scan returns networks and a BT scan returns devices in the same session (one at a time) | Watch for allocation failures — framebuffer + both radio stacks on one chip |
| Wi-Fi scan returns real nearby networks | Scan screen lists actual SSIDs with RSSI, not "coming soon"/empty | — |
| Wi-Fi Monitor collects APs over a multi-minute run without UI stalls | AP list keeps growing/updating; display stays responsive | Single core runs UI + radio; watch for lag under heavy traffic |
| BLE scan returns real nearby advertisers | BT screen lists devices with address/RSSI | — |

## 8. C6 WiFi

| Check | Expected | Pin(s)/Bus |
|---|---|---|
| "WiFi Scan Test" returns a real AP list | SSIDs match what's actually broadcasting nearby | C6 WiFi radio (no dedicated GPIO, on-chip) |
| "WiFi Setup" (SoftAP-based) completes and the device ends up connected to the chosen network | Verify via a subsequent WiFi Scan Test / Errors → Send round-trip | — |
| "WiFi Setup Manual" (on-device password entry) connects successfully | Same as above, using the joystick text-entry grid from section 2 | — |
| "WiFi Monitor" shows real nearby beacon/probe-response traffic, correctly channel-hopping 1-13 | AP list changes as you move / as nearby APs change channel | — |
| WiFi Monitor does not attempt to reconnect STA automatically after stopping (by design) | Confirm this is still true, not an accidental regression | — |

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

- Only the P4's boot/init path has been run against real hardware so far
  (see [KNOWN_ISSUES.md](KNOWN_ISSUES.md)'s Round 13), and that was
  before the Round 15 display swap -- no row in this matrix has actually
  been checked off yet. This matrix is the checklist for working through
  that, not a report of what's already verified.
- Timing-sensitive things (IR NEC frame decode margins, joystick ADC
  debounce/hysteresis thresholds) can only really be tuned against real
  hardware; the host tests in `tests/` intentionally don't try to fake
  those.
- Flash/erase-cycle durability of the RC522 clone write path isn't
  something this matrix attempts to cover — treat repeated clone-testing
  on the same target card as consuming its write endurance.
