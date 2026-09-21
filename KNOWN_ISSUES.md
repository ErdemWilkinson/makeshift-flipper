# Known issues / risks (adversarial review notes)

This file records the findings of a deliberately adversarial static
review of the codebase. Two separate Claude sessions worked on the same
codebase in parallel, so findings from both were merged here.

**Status: every item has been addressed.** Each one was either fixed
with a code change (✅), knowingly accepted as a low-severity risk with
reasoning (🟡/✅ "accepted risk"), or verified and confirmed not to be an
actual conflict/risk (✅ verified). Nothing was closed silently — the
reasoning behind every accepted risk is recorded under that item. One
caveat applies broadly: **the P4 main firmware has been built, flashed,
and booted once on real hardware (Round 13), but that was against the
now-replaced SSD1306 OLED, so the current ST7789 LCD code (Round 15) is
still unverified on physical hardware; the C6 companion firmware remains
entirely unflashed.** Even items marked "fixed" that touch display/UI
code are pending confirmation on the next real flash attempt (see the
General section).

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
- ✅ **FIXED**: `ir_driver.c`'s RX queue depth was bumped from 1 to 3
  (`xQueueCreate(3, ...)`) — back-to-back IR bursts arriving faster than
  the main loop's ~10ms `ir_driver_poll_rx()` polling interval no longer
  silently overflow a depth-1 queue.
- ✅ **FIXED**: `menu_render()` now truncates an over-length label to
  `DISPLAY_COLS - 1` usable columns (accounting for the cursor character)
  with a trailing `"..."` instead of silently cutting it off with no
  indication anything's missing.
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

## Round 8 (this session): submenu system (menu.c/h rewrite) + IR direction
## finding (new ir_direction.c/h)

The menu went from one flat list to a small tree (top-level categories,
each opening its own flat submenu) so new features (IR direction finding
now, more later) don't keep growing a single screen. Self-reviewed since
no other session was available for this round; flagged for a second look
whenever one is.

- ✅ **FIXED (Round 18):** `menu_handle_button()` (`main/ui/menu.c`)
  returns the *next* menu to render but still mutates
  `menu->selected_index`/`scroll_offset` on the menu passed in even for
  UP/DOWN within the same menu — this is intended (it's the same menu
  being mutated), but the split between "mutate in place" and "return a
  different pointer to switch screens" is easy to get wrong if this
  function grows more cases later. A comment-level warning was added
  directly above `menu_t *next = menu;` spelling out the two behaviors
  and what breaks if a future case mixes them. Comment-only change, no
  behavior difference.
- ✅ **FIXED (Round 18):** submenu items and their parent used to be
  wired together only at runtime in `app_main()` via `menu_link_submenu()`
  — if a category item in `s_main_menu_items` was ever added without a
  matching `menu_link_submenu()` call (or the index passed to it drifted
  out of sync with the array, e.g. after reordering items), that item
  would silently do nothing when selected (`on_select` and `submenu` both
  stay NULL, and `menu_handle_button()`'s RIGHT/PRESS case just falls
  through), with no compiler warning either way. Added
  `menu_assert_fully_wired()` (`main/ui/menu.c/.h`) — walks a menu's items
  and `assert()`s none has both `on_select` and `submenu` NULL. Called
  once on `s_main_menu` in `app_main()`, right after all four
  `menu_link_submenu()` calls. Turns a silently-dead button into an
  assertion failure at boot, naming the problem, instead of a user
  eventually finding a menu item that does nothing. Doesn't cover the
  submenus themselves (`s_rfid_menu` etc.) since they're flat leaf-item
  menus with no category items, safe by construction.
- ✅ **VERIFIED, NO RISK:** re-entering a submenu (LEFT then RIGHT back
  into it) re-triggers its entry animation
  (`next->anim_offset_px = ANIM_START_OFFSET_PX` in
  `menu_handle_button()`), but does *not* reset `selected_index` or
  `scroll_offset` — the cursor stays where it was left. This is
  intentional (matches Flipper-style "menu remembers where you were")
  and doesn't conflict with the animation reset, which is purely visual.
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** `ir_direction.c` opens 4 independent
  RMT RX channels on top of `ir_driver.c`'s existing one (5 RMT RX
  channels total once both are active). Neither the ESP32-P4's total RMT
  channel count nor its `mem_block_symbols` budget across that many
  channels has been checked against the datasheet — if the SoC doesn't
  have enough RMT channels/memory for 5 concurrent RX + 1 TX,
  `ir_direction_init()`'s `ESP_ERROR_CHECK(rmt_new_rx_channel(...))`
  calls will abort at boot. This needs checking against the ESP32-P4
  technical reference manual before the first flash, not just assumed.
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** `ir_direction_poll()`'s claim that a
  transmission caught by two adjacent receivers is "the same NEC
  transmission, not two separate ones" assumes the two decodes finished
  within the same ~110ms polling window and reports whichever frame
  happened to be decoded first as `out_frame` (both should carry
  identical address/command, so this is fine in practice, but the
  function doesn't verify the two decoded frames actually match before
  merging their flags — a genuinely simultaneous but different
  transmission from two separate remotes would silently report only one
  frame's payload under a two-direction flag set). Very unlikely
  scenario (two remotes firing in the same ~24ms NEC frame window), not
  worth the added bookkeeping today.
- 🟡 **NOTE, NEEDS HARDWARE VERIFICATION:** the 4 direction-finding GPIOs
  (5, 6, 14, 15) are new, unverified pin choices, same caveat as every
  other pin in this repo — double-check they don't collide with
  strapping pins or another peripheral on your specific ESP32-P4 module
  before wiring up the 4 VS1838B units.
- ❌ **REVIEWED, NO RISK:** the "Ask AI" feature (added by another
  session, restored from a stash last round) doesn't interact with the
  new submenu system in any special way — it's a plain leaf item under
  the top-level menu, unaffected by the tree restructuring.

Second-look verification on Round 8 (this session, `makeshift-flipper-da`):
checked `menu.c/h`, `main.c`'s `app_main()` wiring (`menu_init` /
`menu_link_submenu` calls, ~lines 410-421), `CMakeLists.txt` (confirmed
`answer_view.c` and `ir_direction.c` are both now in `SRCS`), and
`ir_direction.c` against the claims above — all consistent with what was
reported, no discrepancies found. No new issues in the submenu system
itself. One additional finding from reviewing the surrounding code:

- ✅ **FIXED (one-line note added)**: `ir_direction.c` puts two of its
  four receivers on GPIO5/GPIO6 — both inside the ADC1-only GPIO0-6 range
  that `buttons.c`'s own comment identifies as scarce. This doesn't
  conflict with anything today (RMT-RX is a plain digital input), but a
  comment was added next to `GPIO_NORTH`/`GPIO_EAST` explaining the
  tradeoff (forecloses using ADC on those two pins later) so a future
  editor doesn't have to rediscover it by cross-referencing `buttons.c`.
  The pin assignment itself wasn't changed — moving it would just shift
  the scarcity elsewhere on the same GPIO0-6 range.

**[Superseded by Round 11]** The rest of this round's Ask AI findings
(OLLAMA_HOST → Kconfig, ASK_RESPONSE_BUF_LEN bump) and all of Round 9
("Debug AI") documented fixes/risks to code that has since been removed
entirely — see Round 11 below. Removed by `makeshift-flipper-74` in
commit `9d8b15b`; verified against the current tree by
`makeshift-flipper-da` (this session): `OLLAMA_HOST`, `wifi_commands_ask`,
`c6_link_ask`, and `action_ask_ai` no longer appear anywhere in the
source tree (only in this file's history, intentionally, as a record of
what used to exist). `main/diag/diag.c/h` no longer has a single-slot
"last error"; it's the 24-entry ring buffer discussed in Round 11.
`c6-firmware/tools/debug_server.py` no longer calls Ollama. Kept the
paragraph and bullet list above for the historical record (a past review
pass genuinely happened and found real things) rather than deleting it
outright, but none of it describes code that exists in this repo anymore.

- ✅ **FIXED:** `main/ui/answer_view.c`/`.h` (the scrollable text viewer
  built specifically to display Ask AI's answers) were found still
  present on disk and still listed in `main/CMakeLists.txt`'s `SRCS`
  during this cleanup (flagged by `makeshift-flipper-8b`), even though
  `main/main.c` no longer referenced `answer_view` anywhere — Ask AI was
  its only caller, so it was dead code: still compiling, taking up flash
  space, called by nothing. `action_error_history()`'s "Errors" screen
  rolls its own display logic and doesn't need a generic scrollable
  viewer, so both files were deleted and the `SRCS` entry removed by
  `makeshift-flipper-74`.

## Round 9 (historical, superseded)

Removed in its entirety along with the "Debug AI" feature it documented
— see the note above and Round 11 below. The original Round 9 text
described `main/diag/diag.c/h`'s original single-error-slot design,
`c6_link_debug()`/`wifi_commands_debug()`, `c6-firmware/tools/debug_server.py`'s
original Ollama-calling version, and a background `debug_ai_task()` in
`main.c` — none of which exist in the current tree. Removed rather than
kept as dead text since, unlike Round 8's Ask AI paragraph, none of
Round 9's specific findings (single-slot overwrite, missing IR/RDM6300
error sites, `'|'` separator, single-threaded `debug_server.py`, mutex
ordering) apply to anything in the current codebase — `diag.c` isn't
single-slot anymore, `c6_link_debug()` doesn't exist, and
`debug_server.py`'s threading model changed along with everything else
about it. See Round 11 for the replacement feature's own findings.

## Round 10 (this session): RFID clone/dump (main/rfid/rc522.c/h,
## main/main.c's action_rfid_clone()), Wi-Fi Monitor
## (c6-firmware/main/wifi_monitor.c/h, main/net/c6_link.c's monitor API,
## main/main.c's action_wifi_monitor()), and BT Scan
## (c6-firmware/main/bt_scan.c/h, main/net/c6_link.c's bt_scan API,
## main/main.c's action_bt_scan())

Three independent features, all self-reviewed (no other session available
this round); flagged for a second look.

- 🔴 **ACCEPTABLE RISK, MITIGATED BY DEFAULT-OFF:** trailer block writes
  (sector keys + access bits) are never issued by `action_rfid_clone()` --
  `clone_to_card()` is always called with `write_trailers = false`, so
  only the 3 data blocks per sector are cloned, never block 3. Getting a
  trailer's access bits wrong can permanently lock a sector (or, for some
  bit combinations, make Key B unrecoverable). `rc522_write_block()`
  itself has no such guard -- it's a thin primitive that'll happily write
  any block address it's given -- so this protection lives entirely in
  the one call site in `main.c`. If trailer cloning is ever exposed in
  the UI, it needs its own explicit "this is irreversible" confirmation
  screen, not just a flag flip.
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** the default-key dictionary
  (`RC522_DEFAULT_KEYS`, rc522.c) only covers common/factory/publicly-known
  keys. A sector using a private key is left unreadable (`sectors[i].readable
  = false`) and simply skipped in both the dump and the write-back pass --
  `show_dump_and_confirm()` reports "N/16 sectors read" so this is visible
  to the user, but a partially-cloned card with silently-missing sectors is
  the expected outcome for any card that isn't using default keys.
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** `rc522_gen1a_write_block0()` detects
  "not a magic card" only by the absence of a reply to the 0x40 backdoor
  command. Some gen1a clones are pickier about timing/framing than this
  driver's fixed retry loops account for, and gen2 ("CUID") clones use a
  completely different mechanism (normal MFAuthent against block 0, not a
  backdoor) that isn't implemented at all -- a gen2 target card will
  report `is_magic = false` and get no UID clone, indistinguishable in the
  UI from a genuine non-magic card.
- ✅ **FIXED (Round 18):** `action_rfid_clone()`'s dump
  (`rc522_card_dump_t`, 16 sectors x 4 blocks x 16 bytes + bookkeeping,
  ~1.1KB) is heap-allocated (`malloc`) rather than stack, specifically so
  a stack-allocated instance wouldn't blow the calling task's stack --
  there's still no check anywhere in this codebase for how much heap is
  actually free before this runs (unchanged, and not attempted here: a
  pre-check would only reduce the race window, not close it), but a
  `malloc` failure is no longer silent. `diag_record_error("RFID Clone",
  "RC522_CLONE_OUT_OF_MEMORY")` is now called before bailing out to the
  menu, consistent with this action's other two failure modes
  (`RC522_DUMP_NO_SECTORS`, `RC522_CLONE_WRITE_FAILED`) -- a user who hits
  this now sees it in "Errors" instead of the button silently doing
  nothing.
- 🟡 **NOTE, NEEDS HARDWARE VERIFICATION:** the CRC_A hardware co-processor
  flow (`calc_crc()`, `CMD_CALCCRC`/`REG_DIV_IRQ`/`REG_CRC_RESULT_*`) and
  the two-phase WRITE ACK protocol (`transceive_with_crc()`'s `raw_len <=
  1` branch) are both implemented directly from the MFRC522 datasheet with
  no hardware to test against yet -- same blanket caveat as the rest of
  this file's SPI register access, but worth calling out specifically
  since authenticate/read/write are new, higher-risk operations (a bug
  here could corrupt a card, not just fail to read one).
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** Wi-Fi Monitor
  (`c6_link_monitor_start()`) disconnects the C6's STA connection and its
  background `monitor_rx_task` holds `s_link_mutex` for the *entire*
  monitor session, not just a single command -- every other `c6_link_*`
  call (`c6_link_scan`/`connect`/`ask`/`debug`/`setup`, including the
  automatic Debug AI background task) blocks until
  `c6_link_monitor_stop()` is called. This is a deliberate trade-off
  (the alternative is a second concurrent UART reader, which the wire
  protocol doesn't support) and is surfaced in `action_wifi_monitor()`'s
  doc comment and the C6 not auto-reconnecting after, but it does mean an
  error recorded while Monitor is open won't get an automatic Debug AI
  report until the user backs out of the Monitor screen.
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** channel-hopping at 400ms/channel
  across 13 channels (~5.2s per full sweep) can miss or delay seeing an AP
  whose beacon interval doesn't line up with the dwell time -- inherent to
  passive single-radio sniffing, not a bug, but means "AP not listed yet"
  doesn't mean "AP isn't there."
- 🟡 **NOTE, NEEDS HARDWARE VERIFICATION:** `wifi_monitor.c`'s
  `promiscuous_rx_cb()` parses raw 802.11 management frames (fixed offsets
  for addr2/BSSID at byte 10, SSID IE at byte 36) assuming every
  beacon/probe-response has the standard fixed-field layout with no
  optional pre-SSID IEs -- true for ordinary APs but unverified against
  real-world edge cases (some vendors' beacons order IEs differently)
  until tested against real traffic. A frame that doesn't match is
  dropped (`ie_offset` tag byte check), not misparsed, so the failure mode
  is "that AP doesn't show up" rather than corrupted data.
- ✅ **FIXED:** `uart_link_write_line()` (C6 side) had no mutex -- with
  only the single-threaded command dispatch loop ever calling it, this
  was safe by construction. Wi-Fi Monitor's `uart_tx_task` (wifi_monitor.c)
  is a second, independent writer that can now run concurrently with a
  dispatch-loop reply, so a `SemaphoreHandle_t` was added around the two
  `uart_write_bytes()` calls in `uart_link_write_line()` to keep a `PKT:`
  line and e.g. `MONITORSTOP`'s `OK` reply from interleaving into one
  garbled line on the wire. BT Scan's `bt_scan.c` writer reuses the same
  fix, since it's a third concurrent caller of the same function.
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** BT Scan and Wi-Fi Monitor share one
  mutual-exclusion check (`c6_link_monitor_start()`/`c6_link_bt_scan_start()`
  in `c6_link.c` each refuse to start if the other's `s_..._running` flag
  is set) rather than a single shared lock -- there's a narrow window
  between that check and setting the caller's own flag where, in theory,
  both could pass the check before either flag is set if they were called
  from two different tasks simultaneously. In practice both are only ever
  called from `main.c`'s single-threaded menu action flow (one screen
  open at a time), so this isn't currently reachable, but it'd need a
  proper shared mutex if either is ever triggered from a second task.
- 🟡 **NOTE, NEEDS HARDWARE VERIFICATION:** `bt_scan.c`'s NimBLE
  integration (`esp_nimble_hci_and_controller_init()`, passive
  `ble_gap_disc()`, the `sdkconfig.defaults` BT/NimBLE Kconfig options)
  is implemented from ESP-IDF/NimBLE API documentation with no hardware
  to test against yet -- same blanket caveat as the rest of this
  codebase, but worth calling out since this is the first Bluetooth code
  in the project and BLE coexistence with the existing Wi-Fi STA/
  promiscuous code is assumed to work via the IDF's standard coexistence
  handling, not explicitly tested.
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** `bt_scan.c` uses a passive scan
  (`params.passive = 1`) specifically so it never transmits an active-scan
  probe request -- but this also means it can only see whatever a device
  puts in its primary advertising packet. Devices that only reveal their
  name in a scan-response packet (requested by an active scanner) will
  show up with an empty name (`"(no name)"` in the UI) even though they
  do advertise one. Deliberate trade-off to keep this receive-only, not a
  bug.

## Round 11 (this session): removed Ask AI / automatic Debug AI (Ollama
## dependency), replaced with an on-device error history
## (main/diag/diag.c/h, main/main.c's action_error_history()) and an
## optional PC-side upload (main/net/c6_link.c's c6_link_send_error_log(),
## c6-firmware/main/wifi_commands.c's wifi_commands_log_line()/_flush(),
## c6-firmware/tools/debug_server.py sans Ollama)

Ask AI and the automatic Debug AI background task were removed entirely
at the user's request -- both required a PC running Ollama (plus, for
Debug AI, a second always-on Python process) just to make the device
usable at all, which was judged too much setup burden for what the
features were worth. The device now needs no PC/network dependency for
anything except Wi-Fi/BT scanning and the brand-new optional error log
upload. Self-reviewed (no other session available this round, though
makeshift-flipper-8b was notified and is expected to clean up this
round's now-stale predecessors, Round 8's Ask AI items and Round 9 in
full); flagged for a second look.

- 🟡 **ACCEPTABLE RISK, NOT FIXED:** the error history
  (`DIAG_HISTORY_CAPACITY` = 24 entries, `main/diag/diag.c`) lives in RAM
  only -- it does not survive a reboot. A crash or power loss right after
  the most informative error(s) were recorded loses them permanently
  unless the user had already run "Errors" → Send before that point.
  Deliberate: persisting to flash (NVS wear, added complexity) wasn't
  judged worth it for a "what went wrong this session" feature.
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** `diag_entry_t.timestamp_us` comes
  from `esp_timer_get_time()` (microseconds since boot), not a wall-clock
  time -- there's no RTC/NTP on this device. `action_error_history()`'s
  "Xs/Xm/Xh ago" display and `c6_link_send_error_log()`'s `ago_s` field
  are both only meaningful within the current boot session; two entries
  from different boots can't be meaningfully compared, and the uploaded
  `ago_s` says nothing about wall-clock time on the receiving PC (the PC
  side stamps its own `received_at` for that reason).
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** the ring buffer overwrites its
  oldest entry once full (24 recorded) with no warning to the user --
  a burst of errors from one flaky module (e.g. a failing RC522 read
  loop) can silently push older, possibly more useful, entries out
  before the user ever opens "Errors" to see them.
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** the log upload endpoint
  (`debug_server.py`'s `/logs`, `wifi_commands_log_flush()`'s POST) has no
  authentication or transport encryption, same trust model the old Ask
  AI/Debug AI bridge had -- acceptable on a home LAN, not for anything
  more exposed. The uploaded history can reveal what the device has been
  used for (which cards were cloned, which networks/BLE devices were
  scanned), so `error_log.jsonl` should be treated with the same care as
  any other usage log, more so than the old Ollama-bridge questions were.
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** `MAKESHIFT_LOG_SERVER_HOST` is a
  build-time Kconfig value, same "stale IP after DHCP reassignment" risk
  class as the old `MAKESHIFT_OLLAMA_HOST`/`MAKESHIFT_DEBUG_SERVER_HOST` --
  if the configured PC's LAN IP changes, Send just fails (or, in the
  worst case, POSTs to whatever new device now holds that IP) until the
  firmware is reflashed with the correct address. A static DHCP lease on
  the PC avoids this.
- 🟡 **NOTE:** `wifi_commands_log_line()`'s batch buffer
  (`LOG_BATCH_BUF_LEN`, 4KB) can fill before all `LOGSEND:` lines of a
  full 24-entry history arrive if individual JSON objects run unusually
  large (they shouldn't, given the fixed field sizes, but nothing enforces
  it) -- entries past that point are dropped with a warning log, not
  reported to the P4, so a partial upload could be silently incomplete
  from the user's perspective (the P4 only ever sees the final
  `SENT`/`FAIL`, not a partial-count).
- ✅ **FIXED (during this round):** `c6_link.h`'s Wi-Fi Monitor/BT Scan
  section comments referenced `c6_link_ask()`/`c6_link_debug()` (e.g.
  "blocks every OTHER c6_link_* call (scan/connect/send/setup/ask/debug",
  "Ask AI/Debug AI would be functionally fine to run concurrently...") --
  updated to reference the surviving API surface
  (`scan/connect/send/setup/send_error_log`) instead of functions that no
  longer exist, per makeshift-flipper-84's heads-up.

## Round 12 (this session): first real `idf.py build`, host tests + CI, Wi-Fi setup PIN

The first actual compile of both firmwares against ESP-IDF v5.3.1 (all
prior rounds were static-analysis-only, per every "no firmware has been
built" note above). Both now build clean; see commits `57ad832` and
`9644122b`.

- ✅ **FIXED:** `main/ui/display.c` included a nonexistent
  `esp_lcd_ssd1306.h` and `main/idf_component.yml` declared a dependency
  on a component-registry package (`espressif/esp_lcd_ssd1306`) that
  doesn't exist under that name — the actual driver ships inside
  ESP-IDF's own `esp_lcd` component as `esp_lcd_panel_ssd1306.h`. Fixed
  the include and dropped the bogus manifest entry.
- ✅ **FIXED:** six `-Werror=format-truncation` build failures in
  `main/main.c` — `snprintf` precision values (`%.12s`, `%.14s`, etc.)
  that didn't leave enough room in their destination `char[DISPLAY_COLS+1]`
  buffers for the rest of the format string (rssi/channel/"dBm" suffixes),
  plus one literal string one byte over its buffer. All tightened to fit.
- ✅ **FIXED:** `c6-firmware/main/bt_scan.c` called
  `esp_nimble_hci_and_controller_init()`, which no longer exists in this
  NimBLE port (ESP-IDF v5.3) — `nimble_port_init()` now does both
  controller and host init in one call. Updated.
- ✅ **FIXED:** the C6 binary (WiFi + NimBLE BT + HTTP client/server
  together, ~1.37MB linked) no longer fit the default "single app"
  partition table's 1MB app partition. Bumped
  `c6-firmware/sdkconfig.defaults` to the "large" single-app partition
  table (1500K) and 4MB assumed flash size.
- ✅ **FIXED (security):** the Wi-Fi setup AP's password
  (`AP_PASSWORD` in `c6-firmware/main/wifi_setup_ap.c`) was a fixed
  string (`"flipper123"`) baked into the public firmware source —
  visible to anyone reading the repo, identical across every unit built
  from it. Combined with `AP_MAX_CONN=1`'s narrow enforcement gap (it
  only blocks a second association while the legitimate client is still
  associated — if that association drops transiently between the
  setup page loading and the form being submitted, a second attacker in
  range who already knows the fixed password could associate in that
  window and race the real `/connect` POST), this meant the "single
  client" mitigation the code's own comments described wasn't airtight
  against a targeted attacker. Fixed by generating a fresh random
  8-character WPA2-PSK password per setup session on the P4
  (`esp_random()`, hardware RNG) and passing it to the C6 as
  `SETUP:<pin>` instead of a compile-time constant; shown on the OLED
  the same way the old fixed password was, so no UX change. Found during
  a targeted security review of `wifi_setup_ap.c` (this round).
- ✅ **HARDENED (not an active bug):** `wifi_setup_ap.c`'s
  `build_networks_html()` added `snprintf`'s return value to `pos`
  unconditionally across loop iterations; `snprintf` returns the length
  it *would* have written, not what actually fit, so `pos` could end up
  larger than `sizeof(s_networks_html)` — harmless today since `pos` is
  discarded after the function returns and `snprintf`'s own internal
  truncation already protects the buffer, but the classic setup for a
  real overflow if a later edit ever uses `pos` to index/copy. Changed
  to the standard "check the return value, stop on truncation" pattern.
  Found during the same review as the item above.
- Extracted host-testable pure logic (no ESP-IDF dependency) out of
  files that previously mixed it with FreeRTOS/UART/WiFi code:
  `main/net/json_escape.c` (JSON string escaping, out of `c6_link.c`)
  and `c6-firmware/main/text_sanitize.c` (wire-text sanitizing + BLE
  advertising-data name parsing, out of `wifi_monitor.c`/`bt_scan.c`,
  previously near-duplicated between the two). 61 host-side checks
  across 4 test files now cover these plus `diag.c`'s ring buffer and
  `text_entry.c`'s cursor/buffer logic — see `tests/`.
- Added `.github/workflows/build.yml`: CI now builds both firmwares
  (via Espressif's official `esp-idf-ci-action`) and runs the host
  tests on every push/PR.

## Round 13 (2026-09-19): first P4-Pico hardware bring-up

Unlike the older static-only entries, the following findings are based on
actual ESP32-P4-Pico serial logs and a successful build/flash through
ESP-IDF v5.3.5.

- ✅ **SUPERSEDED BY ROUND 15:** the original SSD1306 OLED did not
  acknowledge I2C probes at either `0x3C` or `0x3D` on real hardware
  (reproduced on GPIO7/GPIO8, on a second test pair GPIO14/GPIO15, and
  with an Arduino Uno I2C scanner). Rather than continuing to debug that
  specific display/wiring, the target display was replaced entirely --
  see Round 15: `main/ui/display.c` no longer uses I2C or the SSD1306
  driver at all, it drives a 1.8" ST7789 SPI LCD instead.
- ✅ **SUPERSEDED BY ROUND 15:** the "temporary" GPIO14/GPIO15 test wiring
  noted here is gone -- Round 15's SPI pin plan deliberately reuses
  GPIO14/GPIO15 for the LCD's CS/DC (see that round's pin-conflict note
  for why that's fine given IR direction finding's current state).
- ✅ **FIXED:** the four-receiver IR direction finder has no free RMT RX
  channel once the regular IR receiver/transmitter are running, so
  `ir_direction_init()` is not called from `app_main()` and
  `ir_direction_poll()` always safely reports no frame. The menu item
  ("Infrared" -> "IR Direction Find") is no longer a silent infinite
  wait: `ir_direction_is_available()` (new, `main/ir/ir_direction.h`) lets
  `main.c`'s `render_ir_direction_screen()` detect this and show an
  explicit "Not available on this build" message instead, with the reason
  (RMT channels already used by IR RX/TX) so a user isn't left guessing
  why nothing happens.
- ✅ **FIXED:** boot logs on the first hardware bring-up detected a 32MB
  flash chip while the build assumed 2MB, so ESP-IDF limited itself to
  2MB and logged a mismatch warning every boot. Added
  `sdkconfig.defaults` with `CONFIG_ESPTOOLPY_FLASHSIZE_32MB=y` to match
  the hardware that was actually observed -- deliberately does **not**
  also grow the partition table (nothing currently needs the extra
  space); a fresh `idf.py set-target esp32p4` picks this up automatically
  the same way `c6-firmware/sdkconfig.defaults` already worked. If your
  specific board has different flash, override via `idf.py menuconfig` ->
  "Serial flasher config" -> "Flash size".
- ✅ **NO LONGER STALE:** the General section below and README.md now
  reflect that the P4 main firmware has built, flashed, and booted on
  real hardware (RC522 init, RDM6300 UART init, and P4-to-C6 UART
  initialization all observed) -- this is tracked separately from the C6
  companion firmware's own end-to-end Wi-Fi/BT verification status, which
  remains unconfirmed.

## Round 14 (2026-09-20): RFID UID library + host test/CI fixes

- ✅ **ADDED:** a named RFID/NFC tag library (`main/rfid/rfid_library.c/.h`),
  mirroring `main/ir/ir_library.c`'s shape: "Save 125kHz"/"Save 13.56MHz"
  capture-then-name a scan and persist it to NVS, "RFID Library" browses
  and deletes entries (up to 16 total, either kind). Deliberately
  UID-only -- unlike RFID Clone (a one-shot, not-persisted sector dump),
  nothing saved here can reproduce a card's Mifare data, only recognize
  that the same UID was seen again.
- 🔧 **FIXED, TEST INFRASTRUCTURE:** `tests/run_tests.sh` was missing
  `-I main/net` and `-I main/rfid` on its host-compiler include path, so
  `tests/test_wifi_pkt_parse.c` (`#include "c6_link.h"`) failed to build
  at all. Host test runs skipped straight past this since earlier test
  files didn't need those directories; added both flags plus this round's
  `tests/test_rfid_library.c`, restoring a full, passing host test run.

## Round 15 (2026-09-20): display swapped from SSD1306 OLED to ST7789 SPI LCD

The target display hardware changed: a **1.8" ST7789 SPI LCD, 240x240,
65K colors** (RGB565) replaces the earlier 128x64 monochrome SSD1306 I2C
OLED everywhere in this repo. This is a from-the-ground-up rewrite of
`main/ui/display.c/.h`, not a config tweak.

- ✅ **CHANGED:** `main/ui/display.c/.h` now drive the panel over SPI via
  `esp_lcd_new_panel_st7789()` (built into ESP-IDF's `esp_lcd` component,
  same as the SSD1306 driver was, no extra managed component needed). The
  framebuffer is `uint16_t[240*240]` RGB565 (115200 bytes, in internal
  RAM -- see `display.c`'s comment on moving it to PSRAM if that ever
  becomes tight). `display_init()` fails soft (logs and returns) exactly
  like the old OLED code did if the SPI bus or panel init fails -- drawing
  still works against the in-memory framebuffer, flushing is just a no-op.
- ✅ **CHANGED, API:** `display_draw_text_px()` and `display_fill_rect()`
  now take explicit foreground/background `display_color_t` arguments
  instead of a `bool invert` -- there's no XOR/invert trick on a color
  panel. `display_draw_text()` still exists (now a thin wrapper for
  `DISPLAY_COLOR_TEXT`); `display_draw_text_color()` is new, for picking
  another color explicitly. `main/ui/menu.c` and `main/ui/text_entry.c`
  were updated for the new signatures; `main/main.c` needed no changes
  since it never called the pixel-level API directly (grid-based
  `display_draw_text()` only).
- ✅ **CHANGED, grid size:** `DISPLAY_ROWS`/`DISPLAY_COLS` went from 8x21
  to **15x30** (240px / 16px-tall font = 15 rows, 240px / 8px-wide font =
  30 columns) -- every list screen in `main.c` (IR Library, RFID Library,
  Errors, WiFi Monitor, BT Scan, ...) now shows roughly twice as many rows
  without any code change, since they all compute their visible-row count
  from `DISPLAY_ROWS` rather than a hardcoded 8. Buffer sizes (`char
  line[DISPLAY_COLS + 1]`, etc.) scale the same way automatically.
- ✅ **ADDED:** `main/ui/font8x16_basic.c/.h`, an 8x16 bitmap font replacing
  `font8x8_basic.c/.h` (deleted -- nothing else needs the 8x8 source once
  the 8x16 file exists). Derived by doubling each row of the old font (2x
  vertical scale, same public-domain glyph shapes from the font8x8
  project, Daniel Hepper) -- **not** a proper 8x16 typeface redesign, so
  glyphs read as slightly blocky rather than refined. ASCII printable
  range only (0x20-0x7E); still no Turkish-diacritic (ç ğ ı İ ö ş ü)
  glyph coverage, same gap the 8x8 font had.
- ✅ **ADDED:** a centralized color theme (`DISPLAY_COLOR_*` macros at the
  top of `display.h`): background, text, an amber `ACCENT` for the menu
  selection bar/headers, plus `ERROR`/`OK`/`DIM` for status text that
  `main.c` doesn't use yet (defined for a future round to adopt in error/
  success screens instead of plain white text).
- ⚠️ **PIN REASSIGNMENT, POTENTIAL CONFLICT:** the LCD needs 5-6 SPI pins
  (SCK, MOSI, CS, DC, RST, optionally BL) where the OLED only needed 2
  (SDA, SCL). Landed on SCK=GPIO7, MOSI=GPIO8 (freed by removing I2C),
  CS=GPIO14, DC=GPIO15, RST=GPIO6, BL=GPIO21 -- on its own SPI bus
  (SPI3_HOST) separate from the RC522's SPI2_HOST (GPIO9-13), so there's
  no chip-select contention between the two SPI peripherals. **GPIO14/15
  are the same pins `ir_direction.c` uses for `GPIO_SOUTH`/`GPIO_WEST`.**
  This is not a live conflict today because `ir_direction_init()` is
  already not called from `app_main()` (Round 13, RMT channel exhaustion)
  -- but it means re-enabling IR direction finding on this pin plan now
  requires moving those two receivers to different GPIOs first, not just
  finding spare RMT channels. See the updated README.md pin plan table.
  (The user-facing half of this is now handled: the "IR Direction Find"
  menu screen shows an explicit "Not available" message instead of
  hanging, via the new `ir_direction_is_available()` -- see this round's
  UI-polish entry below. The GPIO conflict itself is unchanged and still
  needs resolving in code before that feature could be re-enabled.)
- ⚠️ **UNVERIFIED ON HARDWARE:** `esp_lcd_panel_invert_color(s_panel, true)`
  is called unconditionally in `display_init()` because most 1.8" ST7789
  modules need it to show correct (non-inverted) colors -- but this
  varies by panel/PCB batch. If colors look inverted on your specific
  module, flip that argument. Likewise, screen orientation
  (`esp_lcd_panel_mirror()`/`esp_lcd_panel_swap_xy()`) is not called at
  all; add it if your panel's factory orientation doesn't match
  "menu text reads left-to-right, top-to-bottom" on first boot.
- ⚠️ **STATIC ANALYSIS ONLY:** like everything else in this repo (see
  Round 13's hardware caveat below), this migration compiles clean under
  ESP-IDF v5.3.1 but has not been run against a physical ST7789 panel.
  `HARDWARE_TEST_MATRIX.md`'s "Power-on and display" section was updated
  with LCD-specific checks (color correctness, orientation, backlight) --
  work through those first on new hardware.

### UI/menu polish that came with the new display

The larger, color panel made a few `main/main.c` cleanups and improvements
worth doing in the same pass, beyond the mechanical color-theme adoption:

- ✅ **ADDED:** `wait_for_any_key()`, replacing 9 separate copies of the
  same 4-line "block until any button is pressed" loop scattered across
  `main.c`'s result screens (RFID Clone, IR Learn, both RFID Save flows,
  WiFi Setup x2, WiFi Monitor/BT Scan start-failure, Errors → Send). Purely
  a duplication cleanup -- behavior is unchanged.
- ✅ **ADDED:** result screens now use `DISPLAY_COLOR_OK`/`DISPLAY_COLOR_ERROR`
  for their outcome line instead of plain text color -- "Saved!"/"Connected!"
  in green, "Failed"/"Library full"/"No networks found" in red, "Cancelled"
  in a dim gray (`DISPLAY_COLOR_DIM`). All 27 screen headers
  (`display_draw_text(0, 0, ...)` calls) were converted to
  `display_draw_text_color(..., DISPLAY_COLOR_ACCENT)` for a consistent
  amber header treatment across every screen.
- ✅ **IMPROVED:** the Errors screen's per-entry line format widened from
  `%.7s %.5s %.7s` (fit for the old 21-column OLED) to `%.12s %.9s %.7s`
  now that `DISPLAY_COLS` is 30 -- module and error-code names are far
  less likely to be truncated illegibly. Still uses `%.*s` precision
  clamps rather than assuming a fit, since `diag.h`'s
  `DIAG_MODULE_MAX_LEN`/`DIAG_CODE_MAX_LEN` allow longer names than any
  single display field here.
- ✅ **FREE WIN, NO CODE CHANGE NEEDED:** every list screen that computes
  its visible-row count from `DISPLAY_ROWS` (IR Library, RFID Library,
  Errors, WiFi Monitor, BT Scan, RC522 sector dump, ...) automatically
  shows roughly twice as many rows now that `DISPLAY_ROWS` is 15 instead
  of 8 -- these screens needed zero code changes to benefit from the
  larger panel, since they were already written against the constant
  rather than a hardcoded row count.
- ✅ **FIXED (follow-up pass):** the RC522 sector-dump screen
  (`show_dump_and_confirm()`) used to show only the first 8 of each
  16-byte block's bytes (`DISPLAY_COLS - 2` clamp, unchanged from the
  OLED era). Now splits each block across 2 rows of 8 bytes so all 16
  bytes of every block are visible -- guarded by a `_Static_assert` that
  the dump body (header + 2 rows/block + footer) still fits within
  `DISPLAY_ROWS` if `RC522_BLOCKS_PER_SECTOR` or the display size ever
  changes. Other screens that could show more per row now but weren't
  specifically revisited in this pass are still worth a look.

## Round 16 (2026-09-20): follow-up cleanup pass on Round 13/15 open items

- ✅ **FIXED:** the ESP32-P4/32MB flash-size mismatch noted in Round 13
  (boot logs detected 32MB, the build assumed 2MB) is now addressed by a
  new `sdkconfig.defaults` at the repo root setting
  `CONFIG_ESPTOOLPY_FLASHSIZE_32MB=y`, matching the hardware that was
  actually observed -- mirrors the pattern `c6-firmware/sdkconfig.defaults`
  already used for its own flash-size fix. Deliberately does not also
  grow the partition table, since nothing currently needs the extra
  space; the app binary still fits comfortably (~62% of the existing
  1MB app partition free). A stale local `sdkconfig` has to be deleted
  once for a fresh `idf.py set-target esp32p4` to pick this default up
  (same one-time caveat the C6 fix already had).
- ✅ **FIXED:** the "IR Direction Find" menu screen used to show
  "Waiting for IR..." forever with no indication the feature was
  disabled on this build. Added `ir_direction_is_available()`
  (`main/ir/ir_direction.h/.c`) so `main.c`'s `render_ir_direction_screen()`
  can detect this and show an explicit "Not available on this build /
  (RMT channels used by IR RX/TX already)" message instead, with a normal
  BACK-to-exit. The underlying GPIO/RMT-channel constraint this reports
  on is unchanged (still documented in Round 13/15) -- this only fixes
  the silent-hang UX, not the constraint itself.
- ✅ **FIXED, DOCUMENTATION:** several places still claimed "no firmware
  in this repo has ever been flashed on real hardware," which stopped
  being true as of Round 13's P4 bring-up. Updated the top of this file,
  the `## General` section below, and `HARDWARE_TEST_MATRIX.md`'s intro
  to state precisely what has and hasn't been verified: the P4's
  boot/init path was flashed once (Round 13), but against the
  now-replaced SSD1306 OLED, so the current ST7789 LCD code is itself
  still unverified, and the C6 companion firmware remains entirely
  unflashed.
- 🔧 **FIXED, BUILD WARNING:** `main/ir/ir_driver.c`'s
  `rmt_copy_encoder_config_t copy_encoder_cfg = {0};` triggered
  "excess elements in struct initializer" on every build --
  `rmt_copy_encoder_config_t` (ESP-IDF's `rmt_encoder.h`) is an empty
  struct with no members, so `{0}` is one initializer element too many
  for it. Changed to `= {}` (an empty initializer list, valid under the
  project's `-std=gnu17`), which is what an empty struct actually wants.
  Purely a warning fix; `rmt_new_copy_encoder()`'s behavior is unchanged
  either way since there was nothing in the struct to initialize.

## Round 17 (2026-09-20): quality status of the OCR and voice TinyML
## companion projects (`ocr/`, `voice/` — separate repos from this
## firmware, not part of the ESP-IDF build)

Not a code review of this repo — a status check on the two ML projects
that are meant to eventually feed this firmware (on-device Turkish line
OCR, and an offline Turkish speech-command classifier), since neither has
a results section anyone had actually read end-to-end. Both are honestly
documented (clear acceptance thresholds, explicit "not deployable" status
markers) — this section exists so that honesty is also visible from the
firmware side, since someone glancing at "Ask AI"/"Debug AI" being removed
here could otherwise assume on-device AI was abandoned rather than moved.

- 🟡 **UPDATE (2026-09-20): now measured, below target but not garbage.**
  `ocr/` (Turkish line OCR, CNN+CTC, full-int8 TFLite) was re-run through
  `scripts/evaluate.py --labels labels.csv` (3681 held-out samples):
  character error rate **13.09%**, exact-line accuracy **57.35%** — below
  its own acceptance bar (CER ≤ 5%, exact-line accuracy ≥ 80%,
  `ocr/README.md`), but most errors are small (a dropped trailing
  word/character, a digit substitution), not wholesale garbage, unlike
  `voice/`'s CTC baseline below. Reads as undertrained/underfit rather
  than architecturally broken — see `ocr/README.md`'s new "Current
  measured results" section for the full numbers and caveats (this is
  still only the training pipeline's own synthetic held-out split, not an
  independent test set). The hardware gap is unchanged and is the harder
  blocker: this firmware still has no camera driver, camera pin
  assignment, frame buffer, or TinyML runtime (TFLite Micro/ESP-DL)
  integrated anywhere — the device's only image-adjacent hardware is the
  display (`main/ui/display.c`), which is output-only. See
  `HARDWARE_INTEGRATION_PLAN.md` (new, this round) for a concrete
  camera + runtime selection and build order — OV2640 (DVP) + TFLite
  Micro (`esp-tflite-micro`) are the recommendations there, with the
  camera's 8-bit data bus **not currently fitting the documented free-pin
  budget** (needs confirming the real P4-Pico's total GPIO count, which
  this repo has never stated, before that's resolved either way).
- 🟡 **UPDATE (2026-09-20): recording tooling added, dataset still not
  collected.** `voice/`'s actual shipped target (12-label fixed command
  classifier, `train.py` — wake word, menu/back/scan/etc, see
  `commands.v1.json`) still has no trained artifact and no recorded
  dataset — that part is unchanged, and collecting it needs an actual
  human recording session, which can't happen from this environment.
  What's new: `voice/scripts/record_commands.py`, an interactive recorder
  that walks a speaker through every (label, style) combination
  `commands.v1.json` needs, writing directly to the
  `data/raw/<speaker>/<style>/<label>/*.wav` layout
  `prepare_dataset.py` already expects (mono 16kHz WAV, keep/redo/skip
  per take, `--resume` to continue an interrupted session). Its own
  acceptance gate (`voice/README.md`) is unchanged: whisper-volume
  accuracy ≥ 85%, normal-volume accuracy ≥ 92%, unknown/silence
  false-accept rate ≤ 2%.
- 🟡 **MEASURED AND POOR, BUT EXPLICITLY NOT THE SHIP TARGET:** `voice/`'s
  separate open-vocabulary CTC baseline (character-level Turkish
  speech-to-text on Mozilla Common Voice, `train_asr_common_voice.py`) has
  been trained and evaluated, and the numbers are bad: character error
  rate 59-65%, word error rate ~100-101% across both the scripted and
  spontaneous-speech eval sets (`voice/artifacts/asr_common_voice_stage2/*.json`)
  — e.g. reference "tabii bu sadece bir ilk adım" decoded as "saramısace
  bi diyikdadı". The artifacts' own metadata already says as much
  (`"status": "baseline_trained_not_device_ready"`,
  `"interpretation": "CTC baseline evaluation; this is not the ESP32
  command-model acceptance metric."`) — flagged here only so "we have a
  trained voice model" isn't read as "we have a working voice model." It
  was never intended to ship; it exists to sanity-check how far a
  from-scratch Turkish CTC model gets on public data, for context when
  judging the command classifier's eventual numbers.
- 🟡 **NOTE:** Both `ocr/` and `voice/` are separate repositories from this
  firmware (their own `.git`, dependencies, and CI), referenced here only
  because they're this device's intended AI features. Nothing in this
  section describes code in `main/` or `c6-firmware/` — there is currently
  no integration code in either firmware tree for OCR or voice commands,
  automatic or otherwise.

## Round 18 (2026-09-21): follow-up on Round 17 — one more voice CTC
## training attempt found, and a bug in its new evaluation script

Checked whether any of Round 17's open items had moved. OCR's numbers
(13.09% CER, 57.35% exact-line accuracy) are unchanged since Round 17 —
nothing new to report there. The voice command classifier (the actual
ship target) still has no recorded dataset and no trained artifact —
also unchanged; `voice/scripts/record_commands.py` (added this round,
see its own commit) is tooling to make that recording possible, not a
substitute for someone actually sitting down and using it.

What's new is a second voice CTC baseline attempt, found already in
progress on disk:

- 🔴 **INCOMPLETE, NOT EVALUABLE:** `voice/artifacts/asr_common_voice_full_v3/`
  (a newer, from-scratch training attempt, different/smaller architecture
  than `stage0-2` — a single Conv1D+BiGRU block per
  `train_asr_common_voice.py`'s `build_models()`, vs. `stage2`'s
  presumably larger network) stopped partway through: it has
  `best_training.keras` (a mid-training checkpoint) and a 10-row
  `history.csv`, but **no `inference.keras`** — the file
  `train_asr_common_voice.py` only writes after its training loop
  finishes normally. Its `.err.log` shows a `No Python at "C:\Users\...\
  python.exe` path error from an earlier, separate failed launch attempt
  (timestamped ~2 hours before the checkpoint files), so the run that
  actually produced `history.csv`/`best_training.keras` happened outside
  that logged invocation and was itself cut short before finishing —
  exactly how or why isn't recoverable from what's on disk. **This model
  cannot be scored**: `evaluate.py`/`evaluate_asr.py` need an inference
  model, and only a mid-training checkpoint with the training-time CTC
  loss layer attached exists. One point of interest for whoever resumes
  this: `history.csv`'s last logged `val_loss` (64.6) is already lower
  than `stage2`'s final `val_loss` (77.4, from `stage2/metrics.json`) at
  fewer epochs (10 vs. `stage2`'s presumably-longer run) — loss values
  aren't comparable across different architectures/data splits with
  certainty, but it's at least consistent with this attempt being on a
  reasonable track before it was interrupted, not a dead end.
- ✅ **FIXED:** `voice/scripts/evaluate_asr.py` called
  `tf.keras.models.load_model(args.model, compile=False)` with no
  `custom_objects` argument, so any `.keras` file saved as a *training*
  model (wrapped in `train_asr_common_voice.py`'s
  `CtcLoss(tf.keras.layers.Layer)`, like `full_v3`'s `best_training.keras`
  above) failed to load with `TypeError: Cannot deserialize object of
  type 'CtcLoss'`. Fixed the same way the training script's own
  `--warm-start` path already did: `evaluate_asr.py` now imports
  `CtcLoss` from `train_asr_common_voice` and passes
  `custom_objects={"CtcLoss": CtcLoss}` into `load_model()`. This didn't
  affect `stage0-2`'s already-reported CER/WER numbers (measured against
  properly-saved `inference.keras` files via a different, unaffected
  path) — it only blocked evaluating a training-format checkpoint like
  `full_v3`'s, which is exactly the case that surfaced it. Note:
  `full_v3` itself is still not evaluable regardless of this fix — see
  the item above, it never produced an `inference.keras` at all, only a
  mid-training checkpoint this fix now lets `evaluate_asr.py` at least
  *load* successfully.

## Round 19 (2026-09-21): pessimistic C6 setup/liveness pass

These were source-level findings from this pass, not exercised on real
ESP32-C6 hardware (this workstation has no Bash/ESP-IDF toolchain to build
and flash with) -- their runtime frequency was unverified, only the faulty
paths' presence in source. Eight items below were fixed in source (not
hardware-verified, same caveat as everything else in this file until
`HARDWARE_TEST_MATRIX.md` is worked through). The stop-path item is only
partially mitigated; its residual post-ACK queue race is reopened in Round
23. This history is kept to distinguish what was addressed from what remains.

- ✅ **FIXED:** `wifi_setup_ap_run()` called `esp_netif_create_default_wifi_ap()`
  every time it received `SETUP:<pin>` (`c6-firmware/main/wifi_setup_ap.c`)
  without saving or destroying the returned default AP netif -- since WiFi
  Setup is an ordinary, repeatable P4 menu action, a second attempt after a
  cancelled/timed-out/failed first one could try to create the same default
  AP netif twice. Fixed: the netif is now created once, lazily, into a
  static `s_ap_netif` and reused on every later call; a creation failure is
  also now handled (logged, setup aborted) instead of discarding the return
  value.

- ✅ **FIXED:** `connect_post_handler()` made exactly one
  `httpd_req_recv(req, body, len)` call and parsed whatever byte count came
  back. `httpd_req_recv()` is allowed to return a partial read (slow phone,
  fragmented TCP), which would have silently truncated the SSID/password
  before parsing. Fixed: now loops (retrying on `HTTPD_SOCK_ERR_TIMEOUT`,
  the standard `esp_http_server` pattern) until the full `content_len` has
  arrived.

- ✅ **FIXED:** the P4 monitor/BT collector task-creation checks
  (`main/net/c6_link.c`'s `c6_link_monitor_start()`/`c6_link_bt_scan_start()`)
  now correctly roll the session back on `xTaskCreate()` returning `pdFAIL`.
  The C6 boot-time Wi-Fi/BLE UART TX tasks (`wifi_monitor_init()`/
  `bt_scan_init()`) retain their task handle and `wifi_monitor_start()`/
  `bt_scan_start()` now refuse to start (return `false`) if that handle is
  `NULL`, instead of reporting `OK` while no task exists to ever forward a
  result to the P4.

- ✅ **FIXED:** `action_rfid_save_1356mhz()` (via the shared
  `wait_for_card()` helper in `main/main.c`) previously only ever returned
  true for `RC522_SCAN_OK`, so a 7/10-byte-UID card
  (`RC522_SCAN_UNSUPPORTED_UID` -- `rc522.c` only implements cascade level
  1) made "Present tag now" hang forever with no way out but BACK, despite
  the screen's own header comment claiming 7/10-byte support (the comment
  was corrected too -- it was aspirational, not backed by
  `rc522_read_uid()`). The same latent hang existed in RFID Clone's two
  `wait_for_card()` calls. Fixed: `wait_for_card()` now reports the
  unsupported case explicitly (a result screen + `diag_record_error()`,
  matching every other failure path in the file) and returns false like a
  cancel, instead of polling forever.

- ✅ **FIXED:** the 125kHz/13.56MHz scan screens treated every ~10ms poll
  that saw a tag as a fresh discovery -- `vibration_pulse(80)` (a blocking
  80ms delay) and, for an unsupported UID, `diag_record_error()` fired on
  every single poll for as long as the tag stayed in the field. Fixed:
  added a present/not-present edge tracker per scan screen (RC522 has a
  real `RC522_SCAN_NO_CARD` signal to reset on; RDM6300 has no such signal,
  so absence is inferred after a run of consecutive silent polls), so
  vibration/diag now fire once per presentation instead of once per poll.

- ✅ **FIXED:** `wifi_commands_scan()` emitted `NET:<raw ssid>,<rssi>`
  straight from beacon bytes with no sanitization, while Wi-Fi
  Monitor/BLE scan already ran untrusted SSID/name text through the
  existing `sanitize_wire_text()` before putting it on the same
  comma/newline-delimited UART line -- an SSID containing a comma or CR/LF
  byte could otherwise have split/corrupted the `SCAN` response and, on
  selection, the `CONNECT:<ssid>,<password>` line sent back for it. Fixed:
  applied the same `sanitize_wire_text()` call to the `SCAN` path. (Lossy,
  same as it already was for Monitor/BLE -- a network whose real SSID needs
  a comma/CR/LF still can't be connected to by name through this protocol,
  but it can no longer corrupt the wire format.)

- ✅ **FIXED:** `wifi_commands_log_flush()` checked its two explicit
  `malloc`s but dereferenced `esp_http_client_init()`'s result
  unconditionally -- under memory pressure this could crash the C6 instead
  of returning the promised `FAIL` response. Fixed: added the missing
  `NULL` check (frees `req_body`/`response_buf`, emits `FAIL`).

- 🟡 **PARTIALLY MITIGATED:** on stopping Monitor or BT Scan, the P4 receiver task sent
  `MONITORSTOP`/`BTSCANSTOP` and trusted exactly one subsequent line to be
  the reply, while the C6's `PKT:`/`BTDEV:` producer task keeps
  independently draining its own queue until `wifi_monitor_stop()`/
  `bt_scan_stop()` actually runs on the C6's dispatch loop -- a queued data
  line could legitimately arrive first, get consumed in place of the real
  `OK`/`FAIL`, and leave that reply sitting in the UART for the *next*
  command to misread as its own. The first half was fixed: both stop paths (`c6_link.c`'s
  `monitor_rx_task()`/`bt_scan_rx_task()`) now skip `PKT:`/`BTDEV:` lines
  and keep reading until the real reply or a timeout. They do not drain
  packets that C6's producer task writes after that reply; see Round 23.

- ✅ **FIXED:** the same problem in reverse at startup --
  `c6_link_monitor_start()`/`c6_link_bt_scan_start()` treated the first
  incoming line as the C6's `OK`/`FAIL` reply, but the C6 enables its
  promiscuous/BLE callback (which can start queuing `PKT:`/`BTDEV:` lines
  for its independent TX task) before its dispatch loop's own `"OK"` write
  runs -- two unordered FreeRTOS tasks, so a data line arriving first could
  make a *successful* start look like a failure. Fixed: both start paths
  now skip `PKT:`/`BTDEV:` lines the same way before checking for `OK`.

## Round 20 (2026-09-21): OCR and voice-pipeline audit

The following are ML-pipeline issues, not evidence that either model is
deployable. No retraining was started; each is directly traceable to the
current scripts.

- ✅ **FIXED (2026-09-21):** `ocr/scripts/train.py` now shuffles `rows`
  with the exact same permutation as `images`/`labels`/`lengths`/`groups`
  (`shuffle_idx = rng.permutation(len(rows))` applied to all of them,
  including `rows = [rows[i] for i in shuffle_idx]`, before `train_idx`/
  `validation_idx` are computed) — the representative-image selection for
  full-int8 quantization calibration now indexes `rows` consistently with
  every other shuffled array, closing the validation-data leak into
  calibration this item originally described.

- ✅ **FIXED (2026-09-21):** `ocr/scripts/evaluate_tflite.py` no longer
  reads the first 40 rows of `data/real_labels.csv` directly. It now
  loads the group-disjoint `"validation"` split persisted in
  `artifacts/split_manifest.json` by `train.py` (exiting with an error if
  that manifest is missing), iterates every row in that split, and
  reports edit-distance-based CER alongside exact-match — the same
  held-out split `evaluate.py`/`train.py` use, not a smoke-test sample.

- ✅ **FIXED (2026-09-21):** `voice/scripts/train.py`'s `report_metrics()`
  now computes per-style accuracy (normal/quiet/whisper individually),
  per-label recall, and the negative-class (unknown/silence) false-accept
  rate, in addition to overall accuracy — matching the release gates
  `voice/README.md` already documented. `main()` also now enforces those
  gates explicitly (`WHISPER_GATE`/`NORMAL_GATE`/
  `NEGATIVE_FALSE_ACCEPT_GATE`) rather than only reporting one aggregate
  number.

- ✅ **FIXED (2026-09-21):** `voice/scripts/train.py` now requires at
  least 3 speakers and builds a genuine 3-way speaker-disjoint split (two
  `GroupShuffleSplit` passes: train+dev vs. held-out test, then train vs.
  dev), tuning only on dev and reporting the final normal/quiet/whisper
  and false-accept numbers separately against the held-out test speakers
  — closing the "model selection and reported score share the same
  validation voices" gap this item described. No trained artifact exists
  yet either way (unchanged — see Round 17/18's still-open "no recorded
  dataset" note); this only fixes the training script's own methodology
  for whenever a real dataset is recorded.

## Round 21 (2026-09-21): allocation and radio-failure audit

- ✅ **FIXED (2026-09-21):** the NULL-handle paths this item originally
  described are now guarded. `c6_link_init()`'s `xSemaphoreCreateMutex()`
  result is NULL-checked (`main/net/c6_link.c`, right after creation,
  logs and aborts init on failure); every later `c6_link_*` call goes
  through a `link_ready()` gate that requires `s_link_mutex != NULL`.
  `s_monitor_data_mutex`/`s_bt_scan_data_mutex` are lazily created with
  their own NULL checks before first use, and both
  `c6_link_monitor_poll()`/`c6_link_bt_scan_poll()` bail out early if the
  mutex is still NULL. On the C6 side, `wifi_monitor_init()` NULL-checks
  its queue/mutex/timer together (logs and disables the feature if any
  failed) and `wifi_monitor_start()` re-checks all of them plus the TX
  task handle before allowing a start; `bt_scan_init()`/`bt_scan_start()`
  follow the same pattern for its queue/mutex. Every one of these paths
  now fails safe (returns `false`/logs, never dereferences a NULL
  handle) instead of assuming allocation succeeded.

- ✅ **FIXED (2026-09-21):** `bt_scan.c` now tracks a
  `static bool s_nimble_ready` flag, set `true` only after
  `nimble_port_init()` succeeds and the host task is launched; if
  `nimble_port_init()` fails, `bt_scan_init()` returns early leaving it
  `false`. `bt_scan_start()` checks `s_nimble_ready` (alongside the TX
  task/queue/mutex) and returns `false` if the BLE stack never finished
  initializing, instead of unconditionally reporting `OK` while no host
  can ever call `on_sync()`.

- ✅ **FIXED (2026-09-21):** both C6 scan consumers now check
  `esp_wifi_scan_get_ap_records()`'s return value.
  `wifi_commands.c`'s scan handler frees its record buffer and replies
  `SCANDONE` (with no `NET:` lines) on failure instead of formatting
  uninitialized records; `wifi_setup_ap.c`'s setup-page scan does the
  same, leaving the rendered network list empty rather than showing
  garbage entries.

## Round 22 (2026-09-21): training and recording-tool reliability

- ✅ **FIXED (2026-09-21):** `voice/scripts/train_asr_common_voice.py`'s
  `BatchSequence.__len__()` now uses ceil division
  (`int(np.ceil(len(self.rows) / self.batch_size))`, with a comment
  noting the old floor-division bug it replaces); `__getitem__()`'s slice
  indexing naturally yields the shorter final batch under normal Python
  slice semantics, so no recording is silently dropped and the reported
  batch/sample counts now match what's actually delivered to Keras.

- ✅ **FIXED (2026-09-21):** `voice/scripts/record_commands.py` no longer
  silently overwrites existing takes on a fresh run — if take files
  already exist for a (speaker, style, label) and neither `--resume` nor
  a new explicit `--replace` flag was passed, it now exits with an error
  instead of overwriting. `--replace` deletes existing files first when
  that's actually intended. `next_take_index()` now scans existing
  filenames and returns the next free numeric index
  (`max(existing) + 1`) rather than a plain file count, so gaps left by
  an interrupted session are no longer overwritten under `--resume`
  either.

## Round 23 (2026-09-21): self-audit correction -- residual C6 UART bleed

- ✅ **FIXED (2026-09-21):** the gap this round originally reported --
  neither `wifi_monitor_stop()` nor `bt_scan_stop()` clearing its producer
  queue before the dispatch loop's stop ACK was written, letting a stale
  `PKT:`/`BTDEV:` line get consumed by the next ordinary command as its
  reply -- is closed. `wifi_monitor_stop()` (`c6-firmware/main/wifi_monitor.c`)
  and `bt_scan_stop()` (`c6-firmware/main/bt_scan.c`) both now take their TX
  task's mutex, `xQueueReset()` the producer queue, and only then release
  the mutex and return; `main.c`'s dispatch loop (`MONITORSTOP`/
  `BTSCANSTOP` handlers) calls these synchronously and writes `"OK"`/
  `"FAIL"` only after they return -- so the queue is already empty and the
  TX task already excluded (via the same mutex) before any ACK reaches the
  P4. No stale data line can follow the ACK. (Originally reported this
  round as an open gap in the Round 19 fix; verified fixed in the current
  source, not just re-asserted -- see the file/function names above.)

## Round 24 (2026-09-21): missing-test and workflow self-audit

- ✅ **FIXED:** `tests/test_wifi_pkt_parse.c` used to contain its own copied
  `parse_pkt_wire_line()` implementation instead of exercising the
  production parser (`static handle_pkt_line()` in `main/net/c6_link.c`),
  so a test pass there was not a real guarantee about shipping behavior.
  Fixed by extracting the parsing logic (BSSID/SSID/RSSI/channel/security
  field extraction, no FreeRTOS/UART dependency) into a new standalone
  module, `main/net/pkt_line_parse.c/h` (same pattern already used for
  `json_escape.c`), which `c6_link.c`'s `handle_pkt_line()` now calls
  directly. `tests/test_wifi_pkt_parse.c` was rewritten to
  `#include "../main/net/pkt_line_parse.c"` directly (matching every other
  test in this suite's "include the real source" pattern) instead of
  reimplementing the parser, and gained 3 new cases (missing "PKT:"
  prefix, malformed/short BSSID, SSID truncation to output capacity) the
  old copy never covered. `main/CMakeLists.txt` updated with the new
  source file. Verified: `bash tests/run_tests.sh` passes (21 checks, 0
  failed) against the actual production parsing function.

- ✅ **FIXED:** `ocr/README.md`'s "Building the dataset" section claimed
  `prepare_real_lines.py`'s output gets "merged into `labels.csv` as
  `real_lines/...` rows" — inaccurate; it actually writes a separate
  manifest, `data/real_labels.csv`, and no merge happens automatically.
  The documented training command sequence also never set
  `OCR_LABELS_CSV`, so following it top-to-bottom trained on synthetic
  data only even after running `prepare_real_lines.py`, silently losing
  the promised real/synthetic calibration balance. Fixed: corrected the
  "Building the dataset" wording to state the two files are separate and
  require `OCR_LABELS_CSV` to combine, and added an explicit
  `$env:OCR_LABELS_CSV = "data\labels.csv;data\real_labels.csv"` step to
  the documented training pipeline (both the synthetic-only default and
  the merged-with-real path are now shown).

- ❌ **NOT REPRODUCIBLE, README AND CODE ALREADY MATCH:** re-checked
  `voice/scripts/train.py`'s `report_metrics()` against `voice/README.md`'s
  claim that whisper- and normal-volume accuracy are reported separately.
  The code already computes `style_accuracy` for `normal`/`quiet`/`whisper`
  individually (plus per-label recall and negative-class false-accept
  rate) — this is the Round 20 fix, confirmed still present. README and
  code are consistent as of this check; no documentation change made. This
  finding likely reflected a moment before the Round 20 fix landed, or a
  different session's snapshot — re-verify against the file, not this
  entry, if this is revisited.

- ✅ **FIXED:** `ocr/README.md`'s "Hardware integration status" section
  still described the device's image-adjacent hardware as a 128x64 SSD1306
  OLED. The firmware moved to a 240x240 ST7789 SPI LCD in
  `KNOWN_ISSUES.md` Round 15; updated the sentence to name the current
  panel and note the SSD1306 was the earlier hardware, no longer
  applicable. Does not change the underlying blocker (still no camera
  driver/pin assignment/frame buffer/TinyML runtime integration anywhere
  in this repo) — only the stale panel description was corrected.

## Round 25 (2026-09-21): UI/menu interaction gaps

- ✅ **FIXED:** the `text_entry` grid used by `action_wifi_setup_manual()`
  was a fixed 6-row keyboard with no space, most punctuation (`! # $ % &
  ' ( ) + , / : ; = > ? [ ] \ ` { | } ~`), or uppercase `K` through `Z` —
  since the entered buffer is passed directly to `c6_link_connect()`, a
  real WPA/WPA2 passphrase using any of those characters could not be
  typed at all, making the manual fallback report a connection failure
  even with the correct password. Fixed: `main/ui/text_entry.c`'s
  `s_grid` grew from 6 to 10 rows (still fits the 240px-tall panel --
  `CELL_Y0_PX(32) + 10*CELL_H_PX(16) = 192 ≤ 240`), adding the missing
  uppercase letters, a two-character `"SP"` cell for space (space itself
  can't be shown on a cell, so it needs a visible placeholder;
  `text_entry_handle_button()` special-cases that exact label to append
  `' '`), and nearly all remaining printable-ASCII punctuation (`*`, `<`,
  `^` stay reserved as CLR/DEL/OK and can't be typed, an accepted
  narrowing). `tests/test_text_entry.c` updated for the new control-row
  position (row 9, not row 5) and extended with cases for the space cell,
  the grid's unused trailing cell (now a documented no-op instead of
  undefined), and reachability of `K` and `~` specifically. Verified:
  `bash tests/run_tests.sh` passes (28 checks in this file, 0 failed).

- ✅ **FIXED:** `action_wifi_scan_test()` used to block for
  `c6_link_scan()` then only log the count/SSIDs/failure via
  `ESP_LOGI`/`ESP_LOGW` before returning to the menu with no on-screen
  indication a scan happened, succeeded, or failed — a device-only user
  with no serial connection saw nothing at all, and this conflicted with
  `HARDWARE_TEST_MATRIX.md`'s expectation that the action shows a real AP
  list. Fixed: now shows a "Scanning..." state, then a result screen —
  the found networks (SSID + RSSI, up to `LIST_VISIBLE_ROWS - 1` with a
  "(+N more, see logs)" note if `C6_MAX_NETWORKS` doesn't fit on screen),
  "No networks found", or a "Scan failed / C6 not responding?" error —
  and waits for `wait_for_any_key()` before returning. Log lines are
  unchanged (still written for every network, not just the shown subset).

- ✅ **FIXED:** `action_ir_send_test()` used to construct and send the
  test frame, log it, and immediately return with no visible confirmation
  the button press was registered or that transmission was attempted.
  Fixed: now shows a brief "Test frame sent / addr=0x00 cmd=0x45" result
  screen and waits for `wait_for_any_key()`. `ir_driver_send()` is still
  `void` (no error return from the driver), so this confirms the request
  was made, not that the IR LED actually emitted — a real failure-state
  improvement would need `ir_driver_send()` to gain an error return first,
  noted here as a smaller residual gap, not left silently unaddressed.

## General

- Both firmwares build clean (see Round 12). The **P4 main firmware** has
  been flashed and booted on real hardware (see Round 13: RC522 init,
  RDM6300 UART init, and P4-to-C6 UART initialization all observed) --
  but that was against the old SSD1306 OLED, before the Round 15 ST7789
  LCD swap, so the new display code itself is still unverified on
  physical hardware (see Round 15's hardware-test caveats). The **C6
  companion firmware**'s end-to-end Wi-Fi/BT behavior remains entirely
  unverified on real hardware. See `HARDWARE_TEST_MATRIX.md` for the
  checklist to work through as each piece gets tested.
- Error reporting mostly goes through `ESP_LOGW`/`ESP_LOGE` only — with
  the device's own display as the primary output, a user who isn't on a
  serial connection won't see these errors at all.

---
*This list merges findings from two parallel adversarial review passes
over the same codebase. Note which item was confirmed/fixed by which
pass when updating or removing entries.*
