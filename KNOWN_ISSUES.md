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

- 🟡 **ACCEPTABLE RISK, NOT FIXED:** `menu_handle_button()`
  (`main/ui/menu.c`) returns the *next* menu to render but still mutates
  `menu->selected_index`/`scroll_offset` on the menu passed in even for
  UP/DOWN within the same menu — this is intended (it's the same menu
  being mutated), but the split between "mutate in place" and "return a
  different pointer to switch screens" is easy to get wrong if this
  function grows more cases later. Worth a comment-level warning for
  future editors, not a behavior bug today.
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** submenu items and their parent are
  wired together at runtime in `app_main()` via `menu_link_submenu()`
  rather than at compile time — if a category item in
  `s_main_menu_items` is ever added without a matching
  `menu_link_submenu()` call (or the index passed to it drifts out of
  sync with the array, e.g. after reordering items), that item silently
  does nothing when selected (`on_select` and `submenu` both stay NULL,
  and `menu_handle_button()`'s RIGHT/PRESS case just falls through). No
  compiler warning either way. A comment was added next to the array
  noting the index dependency, but nothing enforces it.
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
- 🟡 **ACCEPTABLE RISK, NOT FIXED:** `action_rfid_clone()`'s dump
  (`rc522_card_dump_t`, 16 sectors x 4 blocks x 16 bytes + bookkeeping,
  ~1.1KB) is heap-allocated (`malloc`) rather than stack, specifically so
  a stack-allocated instance wouldn't blow the calling task's stack --
  but there's no check anywhere in this codebase for how much heap is
  actually free at that point, and `malloc` returning `NULL` is handled
  (bails out to the menu) but not surfaced to the user beyond just
  silently returning -- worth a `diag_record_error()` call there too if
  this turns out to happen in practice.
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

## General

- Both firmwares now build clean (see Round 12), but neither has been
  **flashed to or run on real hardware** yet — every finding above (other
  than the build fixes themselves) is still the result of static
  analysis; register timing, pin/strapping conflicts, and power
  tolerances are all unverified. See `HARDWARE_TEST_MATRIX.md` for the
  checklist to work through once real hardware is available.
- Error reporting mostly goes through `ESP_LOGW`/`ESP_LOGE` only — with
  the device's own OLED as the primary display, a user who isn't on a
  serial connection won't see these errors at all.

---
*This list merges findings from two parallel adversarial review passes
over the same codebase. Note which item was confirmed/fixed by which
pass when updating or removing entries.*
