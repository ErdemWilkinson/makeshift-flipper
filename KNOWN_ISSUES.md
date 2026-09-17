# Known issues / risks (adversarial review notes)

This file records the findings of a deliberately adversarial static
review of the codebase. Two separate Claude sessions worked on the same
codebase in parallel, so findings from both were merged here.

**Status: every item has been addressed.** Each one was either fixed
with a code change (✅), knowingly accepted as a low-severity risk with
reasoning (🟡/✅ "accepted risk"), or verified and confirmed not to be an
actual conflict/risk (✅ verified). Nothing was closed silently — the
reasoning behind every accepted risk is recorded under that item. One
exception applies everywhere: **no firmware in this repo has been built
or flashed on real hardware**, so even items marked "fixed" are still
pending confirmation on the first real flash attempt (see the General
section).

## main/net/c6_link.c (P4↔C6 UART protocol)

- ✅ **FIXED**: `send_line()` now returns `bool`, checking the byte
  count `uart_write_bytes` actually wrote; every caller
  (`c6_link_scan`/`connect`/`send`/`setup`) checks the return value and
  bails out early with `false`/`-1` on a write failure.
- ✅ **FIXED**: `c6_link_connect()` now rejects an SSID containing a `,`
  before sending the command (~line 110-119), instead of silently
  mis-parsing it on the other end. `c6_link_send()` didn't need an
  equivalent `:` check: the C6 side (`wifi_commands.c`) already only
  treats the first two `:` characters as delimiters and takes the rest
  as `data` verbatim — noted in the code as a comment.
- `read_line()` (~52-71) polls `uart_read_bytes` every 20ms (a short
  blocking read with a timeout, not a real busy-spin — the task sleeps;
  see the TWDT analysis under the wifi_setup_ap.c item). `s_line_buf` is
  global/static with no mutex — under the current usage pattern (every
  `c6_link_*` call comes from one task, from synchronous/blocking menu
  actions) there's never concurrent access, so there's no practical risk
  today. This would become a real issue if a future background task
  (e.g. IR RX) started calling `c6_link_*` — a mutex should be added
  then. **Accepted risk, not triggered by the current architecture.**
- There's no checksum/CRC on the UART link. **Accepted risk:** over a
  short, direct wired connection (breadboard/PCB, not RF), the odds of
  a bit error are very low; seeing a false "OK" is theoretically
  possible but the user can already verify the outcome on the OLED
  (`action_wifi_setup`/`action_wifi_setup_manual` result screen). Adding
  a checksum would mean changing the protocol on both firmware builds —
  more complexity than this risk, at this scale, seemed to justify.

## c6-firmware/main/wifi_setup_ap.c (web-based Wi-Fi setup) — most significant finding

- 🟡 **MITIGATED, NOT FULLY SOLVED**: the setup AP was originally open
  (no password). A WPA2 password was added, shown to the user on the
  P4's OLED during setup. This is marked "mitigated" rather than "fixed"
  for a few reasons:
  1. The password is fixed/hardcoded in the firmware source. This
     repository is public, so the password isn't hypothetically
     recoverable by decompiling the firmware — it's plainly visible in
     the source to anyone who looks. It only stops opportunistic/casual
     connections, not anyone who has seen this repo or a build of it. If
     you build this device, **change `AP_PASSWORD` before relying on it
     for anything**.
  2. **New risk introduced:** the password now has to be kept in sync by
     hand between two places in two different firmware source files
     (there's no shared header between them). If one is updated and the
     other isn't, the user sees the wrong password on screen — a silent,
     hard-to-diagnose bug class.
  3. ✅ **MITIGATED** (second pass): to reduce MITM/sniffing risk, the
     AP's max simultaneous client count was dropped from 2 to 1. Since
     ESP-IDF's softAP doesn't expose a real 802.11 client-isolation
     flag, limiting to a single client at a time prevents a second
     device from joining the setup network and sniffing the `/connect`
     POST request (which carries the real Wi-Fi password in the clear).
     This does not provide transport security (it's still plaintext
     HTTP, no certificate) — it only guarantees "there can't be a second
     listener on the AP," which was judged sufficient at this scale (a
     hobby/home device, where adding HTTPS would be significant
     over-engineering).
- ✅ **FIXED**: `build_networks_html()` now runs SSIDs through an
  `html_escape()` helper before writing them into the page — the XSS
  risk from unescaped SSIDs is closed. Durability note: the escaping is
  applied field-by-field (SSID only), not centrally at render time — if
  a new field (e.g. BSSID/channel) is added to the page later, it could
  be forgotten the same way. No risk today (RSSI is an int an attacker
  can't control), but worth remembering.
- 🟡 **LIKELY NO RISK, UNVERIFIED**: both sides genuinely sleep the task
  while waiting rather than busy-spinning — `xEventGroupWaitBits` on the
  C6 side (wifi_setup_ap.c), and on the P4 side, `c6_link_setup()`'s
  underlying `read_line()` loop, which calls
  `uart_read_bytes(..., pdMS_TO_TICKS(20))`. Both let the idle task run,
  and the default Task Watchdog only monitors idle tasks, so the
  multi-minute wait probably doesn't trigger a reset. **But this rests
  on an assumption:** there's no `sdkconfig` committed in this repo, so
  actual watchdog behavior depends entirely on unverified ESP-IDF v5.x
  defaults, and none of this firmware has been built and run on real
  hardware. **Needs confirming on the first real flash**, not treated as
  settled. Separately (and regardless of watchdog behavior), the P4's
  joystick/menu is genuinely unresponsive for the whole duration — a
  known UX limitation, already called out in the README.
- `wifi_setup_ap_run()` — **remaining, real risk:** even with the
  max-client limit at 1, the connection is still plaintext HTTP; a
  single-client cap prevents sniffing by a second device, it doesn't
  provide transport security (no HTTPS/self-signed cert was added,
  which would likely be over-engineering at this scale).
- ✅ **FIXED**: `url_decode()` now validates hex digits before decoding a
  `%XX` escape — a malformed sequence (missing/non-hex digit, e.g. a
  literal `%` typed into a password) is left as-is instead of being
  silently decoded into the wrong character (a predictable no-op instead
  of a guess).
- ✅ **VERIFIED, NO RISK**: `extract_form_field`'s field-name prefix
  collision was reviewed — the candidate `p` is always right after a
  `&` or the start of the body (i.e. a real field boundary), and the
  `strncmp` + `p[key_len] == '='` check means a field like `"myssid=x"`
  can't accidentally match a lookup for `key="ssid"`. No risk with the
  current two fields (`ssid`, `password`); worth re-checking if a new
  field is ever added whose name happens to be a prefix of another's (no
  such pair exists today).

## main/rfid/rc522.c

- ✅ **FIXED, IN TWO PASSES**: the first pass added error logging to
  `spi_write_reg`/`spi_read_reg`, but `spi_read_reg` still silently
  returned 0 on failure, which `transceive()` then interpreted as a real
  IRQ register value — meaning a genuine SPI/wiring fault was
  indistinguishable from "no card yet," just logged, with no change in
  caller behavior. Fixed in a second pass: an `s_spi_fault` flag was
  added, reset at the start of each scan and checked at the end by
  `rc522_read_uid()` — if an SPI fault is detected, it now returns
  `RC522_SCAN_ERROR` instead of `RC522_SCAN_NO_CARD`, even if a UID was
  read along the way (avoiding a "mix of good and failed reads").
- ✅ **FIXED**: the antenna now starts powered off in `rc522_init()`
  (`rc522_antenna_off()`), and only turns on while the "13.56MHz" scan
  screen is active (`action_nfc_1356mhz()` → `rc522_antenna_on()`),
  turning back off when leaving that screen. `rc522_antenna_on()`/
  `rc522_antenna_off()` were added to the driver's API, called from
  `main.c`. This keeps the antenna's power draw limited to actual use,
  which matters on a LiPo power budget.
- 🟡 **ACCEPTED RISK, NOT FIXED**: there's a BCC check but no CRC_A, so
  double-bit errors aren't caught. Adding CRC_A would mean using the
  MFRC522's hardware CRC co-processor (`CalcCRC` command) and adding a
  step to the `transceive()` flow — adding that complexity before any
  real hardware testing, for a low-probability scenario (double-bit
  errors) beyond what the existing XOR-based BCC already catches, was
  judged disproportionate. First place to look if read reliability turns
  out to be a problem during hardware testing.
- ✅ **FIXED**: `rc522_read_uid()` now returns an `rc522_scan_result_t`
  (`NO_CARD`/`OK`/`UNSUPPORTED_UID`/`ERROR`) instead of a plain `bool`.
  7/10-byte UIDs (detected via the cascade tag `0x88`) are no longer
  confused with "no card" or "error" — the scan screen in `main.c` now
  shows a "7/10-byte UID: no support" message for them.
- ✅ **VERIFIED, NO CONFLICT**: the collision register is cleared both
  before REQA and before anticollision — confirmed against the current
  file, which both sessions were reading from the same single copy on
  disk (no divergent versions); this fix had already been applied.
- ✅ **FIXED**: a missing SSD1306 header (`esp_lcd_ssd1306.h` wasn't
  included, so `esp_lcd_new_panel_ssd1306()` wouldn't have compiled) —
  the include was added to `main/ui/display.c`.

## main/ir/ir_nec.c

- ✅ **FIXED, NO CONFLICT**: an LSB-first bit-order bug (the received
  address/command bytes were swapped during decode — the checksum stayed
  internally consistent, so it silently produced the wrong value) was
  fixed. Confirmed against the current file; both sessions were looking
  at the same single version.

## c6-firmware/main/CMakeLists.txt

- ✅ **FIXED**: the `esp_event` component was missing from `REQUIRES`
  (`wifi_commands.c` uses `esp_event_handler_register()` etc.) — added.

## main/rfid/rdm6300.c

- ✅ **FIXED**: `rdm6300_poll()` is now genuinely non-blocking. It used
  to wait up to 20ms for the remaining 13 bytes once it found the start
  byte; now it only reads whatever bytes are currently ready in the
  UART FIFO (0 timeout) and accumulates them in static state, never
  blocking even if a frame spans two `poll()` calls. API docs were
  updated to match.

## Round 5 (independent review pass)

- ✅ **FIXED**: `wifi_commands.c` and `wifi_setup_ap.c` both used
  `wifi_ap_record_t.ssid` (`uint8_t[33]`) directly with `%s`; ESP-IDF
  does guarantee this array is always null-terminated in practice, but
  it isn't a documented header-level contract. A defensive line was
  added in both places, explicitly null-terminating the last byte right
  before it's used with `snprintf`/`html_escape`.
- 🟡 **ACCEPTED RISK, NOT FIXED**: `vibration_pulse()` blocks the main
  loop (`main.c`) synchronously for 80ms on every successful scan —
  joystick/BACK responsiveness and other polling (IR RX, RFID) are
  delayed during that window. 80ms is below typical human reaction time
  and isn't expected to be noticeable; switching to a non-blocking timer
  was judged unnecessary complexity for the benefit at this scale.
- ❌ **FALSE ALARM, NO FIX NEEDED**: one review pass claimed that the
  designated initializer `wifi_config_t ap_cfg = { .ap = { .ssid =
  AP_SSID, ... } }` in `wifi_setup_ap.c` wouldn't compile, pointing to
  `wifi_commands.c`'s use of `memcpy`/`strncpy` for the same fields as
  supposed evidence. This is incorrect: assigning a string literal to an
  array field inside an *initializer* (the same rule as `uint8_t
  arr[32] = "foo";`) is valid C99/C11 and compiles fine; the
  `memcpy`/`strncpy` in `wifi_commands.c` is needed for a different
  scenario (copying at runtime from a `const char *` parameter, not a
  compile-time constant) — both files are correct and don't contradict
  each other. Confirmed via language-rule analysis (no real C compiler
  available on this machine); no code was changed.

## Round 6 (second reviewing session, files not previously covered:
## ui/menu.c, ui/display.c, ui/text_entry.c, ir_driver.c, buttons.c,
## vibration.c, c6-firmware/wifi_commands.c, c6-firmware/uart_link.c)

- ✅ **FIXED, PARTIALLY**: on the P4 side, `c6_link_connect()`
  (`c6_link.c`) now rejects an SSID over 32 bytes before sending it
  (an early, logged `false` instead of silently attempting to connect
  with a wrong/truncated SSID). Marked "partial" because the C6 side
  (`wifi_commands.c`, `wifi_setup_ap.c`) still silently truncates based
  on buffer size — requests coming through the P4 are now stopped
  earlier, but the web setup form (phone → C6, never passing through the
  P4) still carries the same risk. Adding a client-side `maxlength` to
  the web form, or repeating the same check on the C6 side, would be a
  separate follow-up.
- ✅ **FIXED**: a `mask` parameter was added to `text_entry_render()`
  (`text_entry.h`/`.c`) — when `true`, the entered text is shown as `*`
  characters instead of plaintext. `action_wifi_setup_manual()`'s
  password entry in `main.c` now passes `mask=true`; this never applied
  to the network picker (a list selection, not text entry).
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** `ir_driver.c`'s RX queue
  (`s_rx_queue = xQueueCreate(1, ...)`) is only 1 deep. The main loop
  calls `ir_driver_poll_rx()` roughly every 10ms; if two IR symbol
  bursts arrive close enough together that the queue is still full,
  `xQueueSendFromISR` silently drops the second one (its return value
  isn't checked). NEC repeat codes typically arrive ~110ms apart, so the
  practical risk is low but not zero; bumping the queue depth to 2-3
  would be a cheap improvement.
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** `menu_render()` fits a menu item's
  label into `DISPLAY_COLS` columns, but spends one column on the cursor
  character (`>`/space), so the real usable width is `DISPLAY_COLS - 1`.
  A label longer than that is silently truncated with no `...` or other
  truncation hint. Not an issue for the current 7 fixed menu items in
  `main.c`; could bite unnoticed if a longer item label is added later
  (e.g. a saved IR code's name).
- 🟡 **NOTE, NO FIX NEEDED (already called out in the README, reiterated
  here):** `c6-firmware/main/uart_link.c`'s `UART_TX_GPIO 6`/
  `UART_RX_GPIO 7` are placeholder values. On some ESP32-C6 modules
  these pins can collide with the flash/PSRAM SPI bus or strapping pins
  (board-dependent). The README already flags this in "Next steps";
  don't trust this placeholder without confirming the board's actual
  GPIO6/7 usage on the first flash attempt.
- ❌ **REVIEWED, NO RISK:** `buttons_poll()`'s debounce logic and
  `wifi_commands_connect_sta()`'s `WIFI_FAIL_BIT` event handling were
  both checked further — both looked like potential issues at first
  glance but don't cause a practical problem given the current usage
  pattern (one event per poll, and bits being cleared on the next
  `connect_sta` call).

## Round 7 (this session): main/input/buttons.c/h rewrite for the 2-axis
## analog joystick + separate BACK button (a hardware-driven rewrite by
## the other session, reviewed here for the first time)

Verified first: the three `BUTTON_BACK` call sites in `main.c` (~lines
161, 188, 280), the new ADC-based reading in `buttons.c/h`, the `esp_adc`
component added to `CMakeLists.txt`, and the README update were all
checked against the actual files and are consistent with what was
reported. Findings on the new code itself:

- ✅ **FIXED** (this session): `calibrate_center()` (`buttons.c`) now
  tracks the min/max of the 16 boot samples and logs a warning if the
  spread is too wide to trust as "resting" (the stick was likely held
  off-center at power-on) — this doesn't correct the reading (there's no
  way to know the true center from a noisy sample alone), but at least
  surfaces the problem for anyone with a serial connection instead of a
  silently wrong center. Separately, the computed center is now clamped
  away from both ADC rails (kept at least `ADC_MAX/8` from 0 and from
  `ADC_MAX`), which closes the degenerate-threshold-window risk noted
  below as its own item — a miswired or faulty pot that reads near a
  rail can no longer collapse `axis_update()`'s trigger/release window to
  near-zero width.
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** `read_axis()` (`buttons.c:46-58`)
  silently returns the calibrated center on an ADC read failure (a
  deliberate "reads as no motion" choice), only logging via `ESP_LOGW`.
  If the ADC fails persistently (e.g. a disconnected wire), the joystick
  appears completely dead with no way for the user to tell why without a
  serial connection — the device looks "frozen."
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** center calibration only happens once
  at boot, never re-calibrated at runtime. Cheap potentiometer modules
  can drift over time (heat, mechanical wear); a device left on for a
  long session could develop a "stick pulls to one side" feel with no
  way to correct it short of a reboot.
- ✅ **FIXED** (this session): see the `calibrate_center()` fix above —
  `center` is now clamped away from both ADC rails, so this can no
  longer occur through the calibration path. (A pot that drifts near a
  rail *after* boot, post-calibration, isn't covered by this fix — see
  the "no runtime re-calibration" item above.)
- 🟡 **NOTE, NEEDS HARDWARE VERIFICATION:** `BACK_GPIO` (GPIO23) is a
  newly introduced pin, not present in the previous pin plan. The README
  calls it "unused" but since no firmware here has been built or flashed,
  whether GPIO23 collides with a strapping pin or another peripheral on
  the ESP32-P4 is unverified — worth double-checking specifically on the
  first flash attempt, since it's new rather than carried over.
- ❌ **REVIEWED, LOW SEVERITY:** `buttons_poll()` (~lines 153-167) checks
  PRESS before BACK; if both are pressed in the same poll tick, BACK is
  missed that tick (not lost permanently — it's picked up on the next
  poll). Practical impact is negligible.

## General

- No firmware in this repo has been built or run on real hardware (also
  noted in the README) — every finding above is the result of static
  analysis; register timing, pin/strapping conflicts, and power
  tolerances are all unverified.
- Error reporting mostly goes through `ESP_LOGW`/`ESP_LOGE` only — with
  the device's own OLED as the primary display, a user who isn't on a
  serial connection won't see these errors at all.

---
*This list merges findings from two parallel adversarial review passes
over the same codebase. Note which item was confirmed/fixed by which
pass when updating or removing entries.*
