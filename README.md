# Makeshift Flipper — firmware skeleton

A DIY Flipper Zero-style multi-tool built on an ESP32-P4-Pico, running
ESP-IDF (not Arduino). A 240x240 color SPI LCD, RFID/NFC, IR transceiver,
and a companion ESP32-C6 for Wi-Fi, all driven by a 2-axis analog
joystick plus a separate BACK button.

![System architecture](makeshift_flipper_system_architecture_en_white.png)

## What this can do

- Read 125kHz RFID tags (EM4100 via RDM6300) and 13.56MHz NFC UIDs
  (Mifare via RC522), with haptic feedback on a successful scan
- Dump and clone Mifare Classic 1K cards: authenticate every sector
  against a built-in default-key dictionary, read what it can, and write
  the data blocks onto a second card (RFID / NFC → Clone). UID cloning is
  attempted separately via the gen1a "magic card" backdoor if the target
  supports it; trailer blocks (keys/access bits) are deliberately never
  written to avoid permanently locking a sector — see
  `KNOWN_ISSUES.md`'s Round 10 entry
- Receive, name, save, browse, and transmit infrared remote codes (NEC
  protocol); saved codes persist in NVS. Also includes coarse 4-receiver
  direction finding to tell roughly which way a remote is
  pointed (see `main/ir/ir_direction.c` — needs 4x VS1838B modules, not
  built or tested on real hardware yet)
- Scan and connect to Wi-Fi networks through a companion ESP32-C6 radio,
  either via a phone-based web setup flow or a fully offline
  joystick-driven "scroll keyboard"
- Animated color menu navigation (slide-in entrance, amber-highlighted
  selection bar) on a 240x240 SPI LCD
- **Errors**: a rolling on-device history of the last `DIAG_HISTORY_CAPACITY`
  (24) recorded failures (`main/diag/diag.h`) across every module --
  browse it fully offline (Errors menu), or optionally upload the whole
  list with one button press to a small PC-side log collector over the
  C6's Wi-Fi (no AI/Ollama involved, just an append-only log file) — see
  `c6-firmware/README.md`'s "Error log upload" section
- **Wi-Fi Monitor**: puts the C6 radio into passive promiscuous mode,
  hopping channels 1-13 and listing every AP it sees (SSID/BSSID/RSSI/
  channel/security mode) live on the display (WiFi → WiFi Monitor). Receive-only — no
  deauth or packet injection. Starting it disconnects the C6's STA
  connection and blocks every other Wi-Fi-backed feature (WiFi Scan/Setup,
  Errors → Send) until it's stopped; WiFi Setup needs to be re-run
  afterward if a connection is needed again
- **BT Scan**: passive BLE advertisement scan (Bluetooth → BT Scan) —
  lists nearby BLE devices (address/name/RSSI) live on the display. Never
  connects to anything; observation only, same receive-only scope as Wi-Fi
  Monitor. Can't run at the same time as Wi-Fi Monitor (both need the
  shared C6 UART link exclusively) — see `KNOWN_ISSUES.md`'s Round 10 entry

None of this has been built or flashed on real hardware yet — see
"Known issues / security notes" below and `KNOWN_ISSUES.md` for the full
list of what's verified vs. still assumed.

## Pin plan (matches the `#define`s at the top of `main/input/buttons.c`
and `main/ui/display.c`)

| Signal          | GPIO   | Target            |
|-----------------|--------|------------------|
| 3V3, GND        | —      | LCD, joystick common GND |
| SCK             | GPIO7  | LCD (SPI, its own bus -- SPI3_HOST) |
| MOSI            | GPIO8  | LCD (SPI) |
| CS              | GPIO14 | LCD (SPI) |
| DC              | GPIO15 | LCD (SPI) |
| RST             | GPIO6  | LCD (SPI) |
| BL (backlight)  | GPIO21 | LCD -- tie to 3V3 instead if your module has no BL pin |
| Joystick VRx (X axis, ADC1) | GPIO3 | 2-axis analog joystick module |
| Joystick VRy (Y axis, ADC1) | GPIO4 | 2-axis analog joystick module |
| Joystick SW (center press) | GPIO22 | 2-axis analog joystick module |
| BACK button, standalone | GPIO23 | Extra digital button |
| SPI: CS=9, SCK=10, MOSI=11, MISO=12, RST=13 | — | RC522 (13.56MHz, its own bus -- SPI2_HOST) |
| RX              | GPIO17 | RDM6300 (125kHz) |
| 5V/GND          | —      | LiPo + TP4056 (power input, not software-relevant) |
| TX/RX           | GPIO18/19 | ESP32-C6 (companion radio, UART link) |
| GPIO (via transistor) | GPIO20 | Vibration motor |
| RX (RMT)        | GPIO1  | VS1838B IR receiver |
| TX (RMT, via transistor) | GPIO2 | IR LED transmitter |
| RX (RMT), **not currently wired up** | GPIO5  | VS1838B #2, direction find: North |
| RX (RMT), **not currently wired up** | GPIO6  | VS1838B #3, direction find: East -- **conflicts with LCD RST above** |
| RX (RMT), **not currently wired up** | GPIO14 | VS1838B #4, direction find: South -- **conflicts with LCD CS above** |
| RX (RMT), **not currently wired up** | GPIO15 | VS1838B #5, direction find: West -- **conflicts with LCD DC above** |

The IR pins (GPIO1/2) weren't in the original wiring diagram — they were
picked from free pins. Wire to match, or change `IR_RX_GPIO`/`IR_TX_GPIO`
at the top of `main/ir/ir_driver.c` to your own preference.

**The 4 IR direction-finding receivers (GPIO5/6/14/15) are not called from
`app_main()` on this build** (see `KNOWN_ISSUES.md`'s Round 13 entry — the
P4 has no spare RMT RX channel once the regular IR receiver/transmitter
are running) **and 3 of those 4 GPIOs are now reused by the LCD's SPI
wiring** (CS/DC/RST above). Re-enabling `ir_direction_init()` on this pin
plan is not just a channel-budget problem anymore — it would need the
direction-finding receivers moved to different GPIOs first, since GPIO6/14/15
are physically the same pins the display now needs. If your build re-adds
direction finding, change the `GPIO_NORTH`/`GPIO_EAST`/`GPIO_SOUTH`/`GPIO_WEST`
defines at the top of `main/ir/ir_direction.c` to free pins first.

**Joystick hardware note:** the original plan assumed a 5-pin digital
joystick; the actual hardware is a **2-axis analog joystick module**
(X/Y potentiometer + center button + power LED). Since ADC1 channels on
the ESP32-P4 only live on GPIO0-6 (GPIO0/1/2 are already taken, GPIO7/8
are the LCD's SCK/MOSI), X/Y landed on **GPIO3/GPIO4** — picked from free pins,
wire to match or change the `JOY_X_ADC_CHANNEL`/`JOY_Y_ADC_CHANNEL`
defines at the top of `main/input/buttons.c`. The center button (SW)
stayed on GPIO22. A **separate physical BACK button** was also added
(GPIO23) — the analog joystick's X axis is now used purely for
left/right navigation within a menu, while exiting a screen is this
standalone button's job. Errors are recorded automatically
(`diag_record_error()`) rather than via a GPIO read -- see "Errors" below.

Joystick axis reading: each axis is calibrated at boot (average of 16
samples, since the rest point isn't guaranteed to be dead center), a
direction event fires once a reading strays 3/8 of the way from center
toward an extreme, and it won't re-arm until back within 1/8 of center
(hysteresis, for "button-like" behavior instead of jitter).

Display: a 1.8" ST7789 SPI LCD, 240x240, RGB565 (65K colors) -- see
"Display and UI" below for the driver, color theme, and font details, and
`main/ui/display.h` for the pixel/character-grid constants.

## Menu navigation (Flipper Zero style)

The menu is a small tree, not one flat list: the top level is a set of
categories (**RFID / NFC**, **Infrared**, **WiFi**, **Bluetooth**, plus
the leaf items **Errors** and **About**), and each category opens its own flat submenu
(e.g. **Infrared** → "IR Learn" / "IR Library", **RFID / NFC** → "Save
125kHz" / "Save 13.56MHz" / "RFID Library"). See
`main/ui/menu.c`/`menu.h` — `menu_link_submenu()` wires a category item
to its child menu at startup (`main/main.c`'s `app_main()`), and
`menu_handle_button()` walks that tree.

- **Up/Down** (joystick Y axis): moves the cursor
- **Right** (joystick X axis) or **center press**: on a leaf item,
  activates it (both do the same thing); on a category item, enters its
  submenu
- **Left** (joystick X axis): backs out to the parent menu (a no-op at
  the top level, which has no parent)
- **BACK (separate physical button)**: exits whatever screen/action is
  active (a scan screen, IR direction find, the scroll keyboard, ...)
  straight back to the menu it was launched from — this is a different
  thing from LEFT, which only moves within the menu tree itself and
  never touches an active screen

Note the distinction: LEFT never exits a screen, and BACK never moves
between menu levels — they don't overlap.

## Display and UI

The panel is a 1.8" **ST7789 SPI LCD, 240x240, RGB565 (65K colors)** —
this replaced an earlier 128x64 monochrome SSD1306 OLED (see
`KNOWN_ISSUES.md` for the migration notes and why). `esp_lcd_new_panel_st7789()`
is built into ESP-IDF's `esp_lcd` component, so no extra managed component
is needed (unlike the SSD1306, which briefly needed one).

- **Resolution**: `main/ui/display.h`'s `DISPLAY_WIDTH_PX`/`DISPLAY_HEIGHT_PX`
  (240x240). The framebuffer is a full `240*240` array of `uint16_t` RGB565
  pixels (`main/ui/display.c`'s `s_framebuf`, 115200 bytes) — pushed to the
  panel in one `esp_lcd_panel_draw_bitmap()` call per `display_flush()`.
- **Font**: `main/ui/font8x16_basic.c` — an 8x16 monochrome bitmap font (one
  glyph is 8 bytes wide, 16 rows tall), giving `DISPLAY_ROWS = 15` text
  rows and `DISPLAY_COLS = 30` columns. It's derived from the earlier 8x8
  font by doubling each row (2x vertical scale) — same glyph shapes,
  taller. ASCII-only; there's no Turkish-diacritic glyph coverage yet (a
  gap noted in `KNOWN_ISSUES.md`).
- **Color theme**: centralized as `DISPLAY_COLOR_*` macros at the top of
  `display.h` (background, text, an amber `ACCENT` used for the menu
  selection bar and headers, plus `ERROR`/`OK`/`DIM` for status text) —
  change the palette in one place rather than hunting for scattered color
  literals through `main.c`.
- **Drawing API**: `display_draw_text(row, col, text)` for the common case
  (default text color on the background color); `display_draw_text_color()`
  to pick an explicit color; `display_draw_text_px()`/`display_fill_rect()`
  for pixel-level drawing with explicit foreground/background colors (no
  invert/XOR trick — that was an SSD1306-specific 1-bit shortcut that
  doesn't apply to a color panel).

### Menu animations

- On entry, the list slides in from the right with ease-out easing (a
  slight per-row stagger/cascade, no float math needed by hand) —
  `main/ui/menu.c`'s `menu_animate_tick()`
- The selected row is highlighted as a solid `DISPLAY_COLOR_ACCENT`-filled
  bar with `DISPLAY_COLOR_ACCENT_TEXT` text drawn on top
- Start in `main/ui/menu.c`'s `menu_render()` if you want to add more
  animations or restyle the selection highlight

## Building (ESP-IDF isn't installed on this machine — run this in your
own environment)

```
# Requires ESP-IDF v5.3+ installed and exported (idf.py on PATH)
idf.py set-target esp32p4
idf.py reconfigure
idf.py build
idf.py -p COMx flash monitor
```

## IR module (NEC protocol)

- `main/ir/ir_nec.c` — pure C, hardware-independent NEC encoder/decoder.
  Takes/produces a raw mark/space (microsecond) sequence, has no RMT
  dependency, so it's testable on its own.
- `main/ir/ir_driver.c` — sets up the RMT RX/TX channels, feeds raw
  symbols from the VS1838B into `ir_nec_decode`, and sends
  `ir_nec_encode`'s output out the IR LED on a 38kHz carrier.
- The menu's "IR Send Test" sends a fixed NEC code (addr=0x00, cmd=0x45);
  incoming codes are continuously logged in the background
  (`ir_driver_poll_rx`, in `main.c`'s main loop).
- `main/ir/ir_direction.c` — coarse direction finding using 4 independent
  VS1838B receivers (GPIO5/6/14/15, one per compass point), each running
  its own RMT RX channel and the same `ir_nec_decode()` used by the
  single-receiver driver. `ir_direction_poll()` reports which
  receiver(s) caught a given transmission as a 4-bit flag set (usually
  one, sometimes two adjacent ones for a source near the boundary
  between them) — this is quadrant-level sensing from a wide acceptance
  cone, not a precise bearing angle. Reachable from the menu as "IR
  Direction Find" (under **Infrared**), which shows the live N/E/S/W
  flags and the last decoded frame. Needs the 4-receiver hardware wired
  up and hasn't been tested on real hardware yet.

## RFID modules

- `main/rfid/rc522.c` — MFRC522 SPI driver. Reads a card UID via the
  REQA + anticollision flow; `rc522_read_uid()` returns a
  `rc522_scan_result_t` (`NO_CARD`/`OK`/`UNSUPPORTED_UID`/`ERROR`).
  Currently only **4-byte (single-size) UIDs** are supported — 7/10-byte
  UIDs (cascade tag `0x88`) are detected and reported distinctly as
  `UNSUPPORTED_UID` (not confused with "no card"/error), but their full
  UID isn't read. Also implements Mifare Classic 1K sector
  authenticate/read/write (`rc522_authenticate`/`rc522_read_block`/
  `rc522_write_block`, with hardware CRC_A support via the MFRC522's
  CalcCRC command) and a gen1a magic-card UID-write backdoor
  (`rc522_gen1a_write_block0`) — see "RFID / NFC → Clone" below. The
  antenna is only powered on while a scan/clone screen is active, to save
  power (`rc522_antenna_on()`/`rc522_antenna_off()`, called from `main.c`).
- `main/rfid/rdm6300.c` — RDM6300 UART driver, genuinely non-blocking
  (reads whatever bytes are ready from the UART FIFO with a 0 timeout,
  carrying partial-frame state across poll calls if a frame spans more
  than one). Read-only module; the tag itself streams a 14-byte ASCII
  frame (STX + 10 hex digits + checksum + ETX), which is checksum-verified
  and reduced to a 5-byte EM4100 ID.
- `main/feedback/vibration.c` — drives a vibration motor via GPIO20, an
  80ms pulse fires on a successful read.
- The menu's "Read 125kHz" / "Read 13.56MHz" now open a real scan
  screen: it polls continuously, shows the UID and vibrates once a tag
  is found, and **BACK** returns to the menu (also turns the antenna off
  when leaving the RC522 screen).

## ESP32-C6 link (Wi-Fi companion radio)

**This project contains two separate ESP-IDF firmware builds:**

- `main/` — this main firmware, running on the P4
- `c6-firmware/` — a fully separate project running on the C6 (its own
  `idf.py build`, its own flash). See `c6-firmware/README.md`.

They talk over GPIO18/19 with a simple line-based UART protocol:

```
P4 -> C6: SCAN                          C6 -> P4: NET:<ssid>,<rssi> (x N), then SCANDONE
P4 -> C6: CONNECT:<ssid>,<password>     C6 -> P4: OK or FAIL
P4 -> C6: SEND:<ip>:<port>:<data>       C6 -> P4: SENT or FAIL
P4 -> C6: SETUP                         C6 -> P4: OK or FAIL (can take minutes)
```

- P4 side: `main/net/c6_link.c` — every command is blocking (synchronous),
  waiting for a reply (8s timeout for SCAN/CONNECT, 6 minutes for SETUP)
- C6 side: `c6-firmware/main/wifi_commands.c` — scans/connects using
  `esp_wifi` in STA mode; `SEND` opens and closes a new TCP connection
  each time (no persistent socket)
- The menu's "WiFi Scan Test" just logs the networks it finds — there's
  no real "pick a network and connect" screen for it yet

## Wi-Fi setup (entering a password) — web-based, no keyboard/camera needed

The device has no physical keyboard, and typing a password with the
joystick would be painfully slow, so it borrows the same first-time-setup
pattern most routers use:

1. Select **"WiFi Setup"** from the menu
2. The C6 opens a temporary Wi-Fi network named `MakeshiftFlipper-Setup`,
   WPA2-protected (password shown on the display) and limited to a single
   simultaneous client (AP+STA dual mode —
   `c6-firmware/main/wifi_setup_ap.c`). The password and single-client
   limit exist to reduce the risk of someone else joining the setup
   network and sniffing the request that carries your real Wi-Fi
   password (see "Known issues / security notes" below for the full
   picture — this is a mitigation, not a complete fix)
3. The user connects their phone to that network and opens
   `192.168.4.1` in a browser
4. A simple HTML page (no external libraries/JS) shows the scanned
   networks as a dropdown; the user picks one, enters the password, and
   submits
5. The C6 takes the form data, attempts to connect as an STA, and
   reports the result back to the P4 as `OK`/`FAIL`; the display shows the
   outcome
6. The AP and HTTP server shut down once the connection succeeds, or
   after a 5-minute internal C6 timeout (the P4 side waits up to 6
   minutes)

The P4's main loop **blocks** during this flow (joystick/menu don't
respond) — `action_wifi_setup()` just waits for any keypress once it's
done. This is a deliberate simplification; a non-blocking "setup in
progress" screen could be built as its own task/state machine if needed.

**Note:** a QR-code / camera module alternative was considered but
dropped — it would need extra hardware (a camera), extra cost, and image
processing / QR decode software (which would have been the single most
complex piece of the whole project). The web-based flow gets the same
result with zero extra hardware.

## Wi-Fi setup — no-phone fallback (joystick "scroll keyboard")

For when a phone isn't handy, or web setup isn't wanted, there's a
second, fully self-contained method that only uses the device's own
joystick: **"WiFi Setup Manual"** from the menu.

1. Networks are scanned via the C6 (same `SCAN` command)
2. Results are listed on the display; **Up/Down** picks a network, **Press**
   confirms, **BACK** cancels
3. The selected network's password is entered with the joystick-driven
   "scroll keyboard" in `main/ui/text_entry.c`: a 10x6 letter/digit grid
   navigated with **Up/Down/Left/Right**, **Press** adds a character.
   Special cells: `^` = confirm/submit, `<` = backspace, `*` = clear all
4. On confirm, a `CONNECT:<ssid>,<password>` command is sent to the C6
   and the result is shown on the display

Like the web setup flow, this screen is fully blocking — nothing else on
the device works until it's done. The scroll keyboard was written as a
general-purpose component (`text_entry_t`) and can be reused anywhere
else text entry is needed later (e.g. naming a saved IR code).

## What currently works

- An animated, joystick-driven display menu, organized as a category tree
  (RFID/NFC, Infrared, WiFi, Bluetooth, plus Errors/About) rather than one flat list
- Buttons (joystick directions) are debounced
- IR receive and transmit over NEC, with a joystick-driven **IR Learn** flow
  to name a received code and an **IR Library** screen to browse, send, or
  delete up to 16 saved NVS-backed codes
- IR direction finding (4-receiver quadrant sensing) is implemented in code
  but **not currently wired up** -- `ir_direction_init()` is no longer
  called from `app_main()` because the P4 has no spare RMT RX channel once
  the regular IR receiver/transmitter are running (see Round 13 in
  [KNOWN_ISSUES.md](KNOWN_ISSUES.md)); the "IR Direction Find" menu entry
  will wait forever until this is redesigned
- RC522 (13.56MHz, 4-byte UIDs only) and RDM6300 (125kHz) are wired to
  real scan screens, with haptic feedback, plus a **"Save"** flow on each
  (**RFID Library**) to name and persist up to 16 scanned tag UIDs total
  (NVS-backed, UID only -- no Mifare sector data is ever stored there,
  unlike the separate Clone flow which is a one-shot copy, not a saved
  library entry)
- The P4 ↔ C6 UART protocol is implemented end to end
  (SCAN/CONNECT/SEND/SETUP/ASK), with real Wi-Fi operations on the C6 side
- Web-based Wi-Fi setup (phone → AP → browser → ssid/password form) is
  implemented end to end, reachable from the menu as "WiFi Setup"
- No-phone fallback: joystick "scroll keyboard" for picking a network and
  typing a password ("WiFi Setup Manual")
- "Errors": browses a rolling on-device history of recorded failures
  (`action_error_history()` in `main.c`, `main/diag/diag.c`'s ring
  buffer) -- works fully offline. An optional "Send" (PRESS) uploads the
  list to a small PC-side log collector (`c6-firmware/tools/debug_server.py`,
  no AI involved) over the C6's Wi-Fi link -- see `c6-firmware/README.md`'s
  "Error log upload" section
- "RFID / NFC → Clone": dump-and-clone flow for Mifare Classic 1K cards
  (`action_rfid_clone()` in `main.c`) — scan a source card, authenticate
  and read every sector the default-key dictionary can unlock, review the
  dump sector-by-sector on the display, then place a target card to write
  the data blocks (never trailers) onto it, plus a best-effort gen1a UID
  clone
- "WiFi → WiFi Monitor": passive promiscuous AP scan
  (`action_wifi_monitor()` in `main.c`, `wifi_monitor.c` on the C6 side)
  — live SSID/BSSID/RSSI/channel/security list, channel-hopping 1-13, no
  transmit/injection. Disconnects the C6's STA link while running and
  blocks every other C6 feature until stopped (see `c6_link.h`)
- "Bluetooth → BT Scan": passive BLE advertisement scan
  (`action_bt_scan()` in `main.c`, `bt_scan.c` on the C6 side, NimBLE) —
  live address/name/RSSI list. Never connects to anything; can't run
  alongside WiFi Monitor (both need the C6's UART link exclusively)
- "About" still just logs (no real screen yet)

## Next steps (not yet written)

1. RC522: full read support for 7/10-byte UIDs (cascade level 2/3) --
   Mifare Classic sector read/write/clone (4-byte UIDs) is now implemented,
   see "RFID / NFC → Clone" above
2. Replace `action_wifi_setup()`'s blocking wait loop with a background
   "setup in progress" state that doesn't block the joystick from
   interacting with other screens (optional improvement)
3. IR direction finding: wire up the 4x VS1838B receivers on
   GPIO5/6/14/15 (see the pin plan above) and verify `ir_direction.c` on
   real hardware — the quadrant flags and acceptance-cone overlap
   behavior are unverified assumptions until then
4. On the C6 side, `c6-firmware/main/uart_link.c`'s GPIO6/7 assumption
   needs checking against your actual wiring (the P4 side is fixed at
   GPIO18/19; the C6 side depends on your board)
5. None of these firmware builds have been compiled or tested on real
   hardware yet — expect to revisit register addresses, timing
   tolerances, and pin assumptions on the first build/flash attempt
6. The analog joystick's X/Y wiring (which pin goes to VRx/VRy) needs
   physical verification — if the axes read backwards, swap the
   `BUTTON_LEFT/RIGHT`/`BUTTON_UP/DOWN` mapping given to X/Y inside
   `buttons_poll()` in `main/input/buttons.c`
7. The threshold/hysteresis constants (`THRESHOLD_FRACTION_*`,
   `RELEASE_FRACTION_*` in `buttons.c`) will need tuning on real
   hardware — depending on the potentiometer's noise floor and mechanical
   play, they could end up too sensitive (false triggers) or too strict
   (missing light touches)
8. "Errors" → Send needs a log server host set via `idf.py menuconfig`
   (`MAKESHIFT_LOG_SERVER_HOST`, `c6-firmware/main/Kconfig.projbuild`) and
   `debug_server.py` running on that PC to do anything — see
   `c6-firmware/README.md`'s "Error log upload" section. A stale address
   (e.g. after a DHCP reassignment) makes Send fail with no more specific
   reason shown — see `KNOWN_ISSUES.md`. None of this is required for the
   on-device Errors history itself, which works fully offline.
9. The error history (`main/diag/diag.h`) is a fixed-size ring buffer of
    the last `DIAG_HISTORY_CAPACITY` (24) entries, kept in RAM only -- it
    doesn't survive a reboot, and older entries are silently dropped once
    it's full. `Errors` → `Send` is the only way to get a persistent
    off-device copy (the PC's `error_log.jsonl`)
10. On-device offline speech-to-command (TinyML keyword spotting for
    Turkish digits/commands via a microphone) is a separate, not-yet-started
    piece — would need new hardware (I2S mic), a new pin, an
    `esp-tflite-micro` integration, and a trained `.tflite` model (that
    training happens off-device, in Python, not on the ESP32 itself)
11. "WiFi Monitor" and "RFID / NFC → Clone" are both untested on real
    hardware, same caveat as everything else here — the CRC_A/authenticate/
    write flow and the raw 802.11 frame parsing (`wifi_monitor.c`'s
    `promiscuous_rx_cb()`) are implemented directly from datasheets/specs
    with no hardware to verify against yet, see `KNOWN_ISSUES.md`'s Round
    10 entry

## Known issues / security notes

This project went through several rounds of adversarial code review
(two parallel Claude sessions deliberately looking for bugs and security
issues in each other's — and their own — work). The full record is in
`KNOWN_ISSUES.md`, including what was fixed, what was accepted as a
low-severity risk (with reasoning), and what's still unverified pending
a real hardware test.

**Headline points, since this repo is public:**

- ✅ **FIXED**: the Wi-Fi setup AP used to have a fixed, hardcoded password
  baked into the firmware source (`AP_PASSWORD` in
  `c6-firmware/main/wifi_setup_ap.c`) — since this repo is public, that
  password was visible in plain text to anyone reading the code, and being
  fixed across every unit meant an attacker who'd learned it once (or a
  second attacker in radio range during someone else's setup window, if
  the legitimate client's association happened to drop) could join and
  race the real request. The P4 now generates a fresh random 8-character
  WPA2-PSK password per setup session (`esp_random()`, the hardware RNG)
  and sends it to the C6 as part of `SETUP:<pin>`; it's shown on the display
  the same way the old fixed one was, so there's no UX change, just no
  more shared/guessable password.
- The Wi-Fi setup flow is **plain HTTP, no TLS** — the setup AP is
  limited to a single simultaneous client specifically to reduce the
  window for a second device to join and sniff the request that carries
  your home Wi-Fi password, but this is not a substitute for real
  transport security.
- There's **no checksum/CRC on the P4↔C6 UART link** — accepted as a
  low risk for a short, direct wired connection, but worth knowing if
  you extend that link.
- **Both firmwares build clean under ESP-IDF v5.3.1** (`idf.py build`,
  verified) **but neither has been flashed to or run on real hardware
  yet** — all of the behavioral claims above (and everything in
  `KNOWN_ISSUES.md` besides the build itself) is the result of static
  review, not a verified test. See `HARDWARE_TEST_MATRIX.md` for the
  checklist to work through once real hardware is available.
