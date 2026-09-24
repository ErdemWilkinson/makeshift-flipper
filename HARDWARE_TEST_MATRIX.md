# Hardware test matrix

A checklist for verifying this device on real hardware, module by module,
after any firmware change that touches pins, the joystick, IR, RFID, or the
Wi-Fi/BLE radio. Pairs with the pin map in
[C6_STANDALONE_HARDWARE_PLAN.md](C6_STANDALONE_HARDWARE_PLAN.md).

**This matrix covers the single-MCU ESP32-C6 standalone build only.** The
former two-chip (ESP32-P4 + ESP32-C6-over-UART) design is retired; if you
find a row anywhere in this repo's history that references P4 GPIOs, an
analog ADC joystick, or a SoftAP-based Wi-Fi setup server, it describes that
retired design, not the current firmware — see
[KNOWN_ISSUES.md](KNOWN_ISSUES.md)'s Round 27 for how that mismatch was
found and fixed.

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
| LCD renders text and colors cleanly, no garbled/mirrored pixels | Menu items readable, correct colors (not swapped R/B) | SCK=GPIO18, MOSI=GPIO19, CS=GPIO9, DC=GPIO8, RST=GPIO20 (shared SPI2 bus with RC522) |
| Colors are correct, not inverted (e.g. background isn't white when it should be black) | If colors look inverted, toggle `esp_lcd_panel_invert_color()`'s argument in `display_init()` -- this varies by panel batch | — |
| Display orientation looks right for your specific panel (not upside-down or mirrored) | If wrong, adjust `esp_lcd_panel_mirror()`/`esp_lcd_panel_swap_xy()` calls (not currently called -- add if needed) | — |
| Backlight turns on with the device | Screen is lit on power-up | Tied to 3V3 (no dedicated backlight GPIO on this build) |

## 2. Joystick and buttons

| Check | Expected | Pin(s) |
|---|---|---|
| UP moves the cursor up | Cursor moves | GPIO4 (direct input) |
| PRESS (center) selects/activates | Menu action fires | GPIO5 (direct input) |
| DOWN moves the cursor down | Cursor moves | TCA9554 expander, IO3 (I2C) |
| LEFT backs out to the parent menu | Menu pops back a level | TCA9554 expander, IO5 (I2C) |
| BACK exits the current screen/action back to the launching menu | Returns to the menu that opened the screen | TCA9554 expander, IO4 (I2C) |
| RIGHT works even though it shares the physical I2C SDA line | Cursor moves right/selects; no I2C errors logged while held | GPIO22 (I2C SDA), sampled directly -- `buttons.c` skips any TCA9554 transaction while this line reads low |
| Expander (DOWN/LEFT/BACK) keeps working normally when RIGHT is not pressed | No missed or phantom expander events | I2C: SDA=GPIO22, SCL=GPIO23 |
| If the TCA9554 read ever fails (logged as a warning), UP/PRESS/RIGHT still work and the expander recovers on its own within ~1s | Device stays usable, no crash, no need to reboot | — |
| Text entry grid (`text_entry.c`, used by WiFi Setup Manual's password screen) navigates and appends chars correctly | Matches `tests/test_text_entry.c`'s host-tested logic | via same joystick pins |

Note: this is the single highest-risk wiring point in the whole build --
RIGHT and the TCA9554's SDA line are electrically the same net by design
(see `buttons.c`'s top-of-file comment and
`C6_STANDALONE_HARDWARE_PLAN.md`). Do not attach any other load to
GPIO22/GPIO23.

## 3. Vibration motor

| Check | Expected | Pin(s) |
|---|---|---|
| Motor pulses on the events `vibration.c` is wired to (check call sites — no dedicated menu action) | Physical buzz felt | GPIO3 (via transistor driver; motor must never connect directly to the GPIO) |
| A flyback diode is present across the motor | No voltage spike visible on the GPIO line when the motor stops | — |
| No motor buzz bleeding into I2C/SPI lines (check wiring layout, not just firmware) | Clean display/RFID reads while motor fires | — |

## 4. IR: transmit + receive

| Check | Expected | Pin(s) |
|---|---|---|
| "IR Send Test" transmits a recognizable NEC frame | Verify with a second IR receiver, phone camera (IR LEDs show as faint purple on most phone cameras), or a known target device (e.g. TV power) | TX=GPIO17 (via transistor driver) |
| IR receiver picks up an incoming NEC frame | Decoded frame shown/logged | RX=GPIO14 (VS1838B OUT) |
| "IR Learn" captures a remote button press and saves it under a chosen name | Entry appears in "IR Library" afterward, survives a reboot | RX=GPIO14 |
| "IR Library" replays (PRESS) a saved code | Target device reacts the same as to the original remote | TX=GPIO17 |
| "IR Library" delete (LEFT) removes an entry and persists the change | Entry gone from the list after a reboot | — |
| "IR Learn"/"IR Library" behave correctly at capacity (16 entries) | "Library full" shown, existing entries untouched, no crash | — |
| ⚠️ **"IR Direction Find" is not available on this build** — `app_main()` deliberately never calls `ir_direction_init()` (this pin map has no spare RMT RX channels once the regular IR receiver/transmitter are running), and the screen says so explicitly instead of hanging. Do not treat this as a bug to fix; it's a feature kept out of this hardware profile. | Screen shows "Not available on this build" | — |

## 5. RFID/NFC: RC522 (13.56MHz)

| Check | Expected | Pin(s) |
|---|---|---|
| "Read 13.56MHz" detects a MIFARE Classic card in range | UID displayed | SCK=GPIO18, MOSI=GPIO19, MISO=GPIO6, CS=GPIO7, RST=GPIO2 (shared SPI2 bus with the LCD; separate CS) |
| SPI wiring doesn't conflict with the LCD (shared SCK/MOSI, separate CS) | Clean reads on both display and RC522, no bus contention | GPIO18/19 shared; GPIO9 (LCD CS) vs GPIO7 (RC522 CS) must toggle independently |
| "Clone (13.56MHz)" successfully reads a source card's sector data | No `RC522_DUMP_NO_SECTORS` in Errors | same SPI pins |
| "Clone (13.56MHz)" successfully writes to a target (writable) card | Target card's UID/data matches source afterward, verify with "Read 13.56MHz" | same SPI pins |
| Clone failure path records a diag entry with a specific failure reason rather than silently doing nothing | Check "Errors" menu shows `RC522_CLONE_WRITE_FAILED` when write fails (e.g. read-only card) | — |
| 7-byte/10-byte UID cards are reported as unsupported, not misread as a shorter UID | "Errors" shows `RC522_SCAN_UNSUPPORTED_UID`, on-screen message reads "7/10-byte UID: N/A" | — |
| "Save 13.56MHz" saves a scanned UID under a chosen name (UID only, no sector data) | Entry appears in "RFID Library" marked 'H' (high frequency), survives a reboot | same SPI pins |
| If the RC522 fails to attach to the shared SPI bus at boot, the rest of the device (display, buttons, IR) still comes up | No crash, RFID menu items fail cleanly instead | — |

## 6. RFID: RDM6300 (125kHz)

> ℹ️ **UART1 function-clock hang fixed (KNOWN_ISSUES.md Round 28).**
> `rdm6300_init()` no longer hangs the watchdog -- the cause was UART1's
> function clock being left gated by ESP-IDF, not a GPIO conflict, and
> `rdm6300_init()` now enables it before `uart_driver_install()`. RX has
> moved to **GPIO1 (Pico GP28)**. Wire the RDM6300 TX line (through the
> level shifter) to GP28 before testing this section.

| Check | Expected | Pin(s) |
|---|---|---|
| "Read 125kHz" detects a 125kHz EM4100-family tag in range | Tag ID displayed | RX=GPIO1 (UART1, receive-only module; RDM6300 is 5V, needs a level shifter down to 3.3V) |
| Read is reliable across multiple tag presentations, not just the first one after boot | Repeated reads work without a reboot | GPIO1 |
| "Save 125kHz" saves a scanned tag ID under a chosen name | Entry appears in "RFID Library" marked 'L' (low frequency), survives a reboot | GPIO1 |
| "RFID Library" delete (LEFT) removes an entry (either kind) and persists the change | Entry gone from the list after a reboot | — |
| "Save 125kHz"/"Save 13.56MHz" behave correctly at capacity (16 entries total) | "Library full" shown, existing entries untouched, no crash | — |

## 7. On-chip radio bring-up (single MCU)

There is no second chip and no UART link — Wi-Fi and BLE run directly on
the same ESP32-C6 as the UI (`main/net/c6_link.c`, `main/net/radio_ble.c`).
These rows check that the on-chip radio actually initializes and coexists
with the display and the rest of the UI on one core.

| Check | Expected | Notes |
|---|---|---|
| Wi-Fi + BLE both initialize at boot | No radio-init error in the log | Watch for allocation failures — framebuffer + both radio stacks on one chip |
| Wi-Fi scan returns real nearby networks | Scan screen lists actual SSIDs with RSSI | "WiFi Scan Test" |
| Wi-Fi Monitor collects APs over a multi-minute run without UI stalls | AP list keeps growing/updating; display stays responsive | Single core runs UI + radio; watch for lag under heavy traffic |
| BLE scan returns real nearby advertisers | BT screen lists devices with address/RSSI | "BT Scan" |
| Wi-Fi scan/connect, Wi-Fi Monitor, and BT Scan are confirmed mutually exclusive (starting one while another runs fails cleanly, not silently) | The busy mode's UI shows/logs a clean rejection, not a hang or corrupted state | See `c6_bt_scan_is_running()`/`c6_wifi_monitor_is_running()` cross-checks in `main/net/c6_link.c` and `main/net/radio_ble.c` |
| Leaving Wi-Fi Monitor and immediately using WiFi Setup Manual to join a network succeeds reliably, not just sometimes | Connect succeeds even right after a monitor session, not only after waiting a few seconds first | **Known race, see [KNOWN_ISSUES.md](KNOWN_ISSUES.md) Round 27**: `c6_link_monitor_start()`'s `esp_wifi_disconnect()` is asynchronous; its delayed `WIFI_FAIL_BIT` can land after a following `c6_link_connect()` call, making a successful join look like a failure. If this row fails, that's the known cause — not a new bug. |

## 8. C6 Wi-Fi menu

| Check | Expected | Pin(s)/Bus |
|---|---|---|
| "WiFi Scan Test" returns a real AP list | SSIDs match what's actually broadcasting nearby | C6 Wi-Fi radio (no dedicated GPIO, on-chip) |
| "WiFi Setup Manual" (on-device password entry) connects successfully | Device gets an IP; confirm via a subsequent WiFi Scan Test | Uses the joystick text-entry grid from section 2 |
| "WiFi Monitor" shows real nearby beacon/probe-response traffic, correctly channel-hopping 1-13 | AP list changes as you move / as nearby APs change channel | No Wi-Fi country is configured, so the regulatory channel set is whatever ESP-IDF's default allows -- a channel your regulatory domain disallows will fail to hop, get logged (`ESP_LOGW`), and quarantined (skipped) after 3 consecutive failures per session, rather than being retried forever or silently skipped with no record. See `hop_timer_cb()`/`s_channel_quarantined` in `main/net/c6_link.c`. |
| Each listed AP shows a short vendor label derived from its BSSID's OUI (e.g. "TP-Link", "Netgear"), or "?" for an unrecognized OUI | Label matches the AP's actual known hardware vendor where the OUI is in the built-in table | `oui_vendor_lookup()` in `main/net/c6_link.c` — a small built-in table (~25 entries), not a full IEEE OUI database, so "?" for most consumer APs is expected and not a bug |
| WiFi Monitor does not attempt to reconnect STA automatically after stopping (by design) | Confirm this is still true, not an accidental regression | — |
| "WiFi Setup" (the old SoftAP-based flow) is understood to be a stub on this build | Selecting it shows "Not available / Use Setup Manual" — this is expected, not a bug (see [KNOWN_ISSUES.md](KNOWN_ISSUES.md) Round 27) | `c6_link_setup()` always returns false |

## 9. C6 Bluetooth (NimBLE, passive scan)

| Check | Expected | Pin(s)/Bus |
|---|---|---|
| "BT Scan" lists real nearby BLE advertisers | Names/addresses match known nearby devices (phone, earbuds, etc.) | C6 BLE radio (on-chip) |
| Scan is genuinely passive — does not itself become discoverable or connectable | Verify with a second phone/BLE scanner that this device doesn't show up as advertising | `radio_ble.c` sets `params.passive = 1` |
| WiFi Monitor and BT Scan are confirmed mutually exclusive | Attempting to start one while the other runs fails cleanly, not silently | Cross-checked in both directions in `main/net/c6_link.c` and `main/net/radio_ble.c` |

## Known gaps not covered above

- No hardware bring-up has happened yet against this pin map — every row
  above is unverified. This matrix is the checklist for working through
  that, not a report of what's already verified. See
  [KNOWN_ISSUES.md](KNOWN_ISSUES.md)'s Round 27 for the audit that found
  this matrix itself was out of date (still describing the retired P4
  pin map) and produced this rewrite.
- Timing-sensitive things (IR NEC frame decode margins, button debounce
  thresholds) can only really be tuned against real hardware; the host
  tests in `tests/` intentionally don't try to fake those.
- Flash/erase-cycle durability of the RC522 clone write path isn't
  something this matrix attempts to cover — treat repeated clone-testing
  on the same target card as consuming its write endurance.
- The PC-side "Errors → Send" log upload from the old two-chip design is
  not part of the standalone build and has no row here; the on-device
  "Errors" history works fully offline instead.
