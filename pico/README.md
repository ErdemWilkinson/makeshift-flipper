# Pico port (in progress, not yet hardware-validated)

Raspberry Pi Pico (RP2040) / Pico SDK replacement for the ESP32-P4 main
MCU in [`../main/`](../main/), built to use the **Waveshare Pico-LCD-1.3**
display module (240x240 ST7789, with a built-in digital 5-way joystick and
4 user buttons — no separate analog joystick module needed). The ESP32-C6
companion ([`../c6-firmware/`](../c6-firmware/)) and its UART wire
protocol are **completely unchanged** by this migration; only the main MCU
is being swapped.

`../main/` stays intact and buildable throughout this port — nothing in
this directory touches it.

> **Status: mid-port, nothing has been built or flashed yet.** Everything
> in this directory is source/build files prepared without a Pico SDK
> toolchain or physical hardware available in the preparing environment.
> Compiling, flashing, and hardware verification are done by whoever picks
> this up next. Do not treat anything below as field-proven until it has
> passed its phase's checkpoint on real hardware.

## Why a new MCU

The Pico-LCD-1.3 module only fits a real Raspberry Pi Pico's header —
different chip family and toolchain from the ESP32-P4 — so using it means
replacing the main MCU, not just rewiring a new screen.

## Current progress: Phase 1 of 8

This port follows a phased plan (pin budget, module-by-module porting
notes, NVS-replacement design, the IR/PIO rewrite approach, and the full
8-phase rollout order with hardware checkpoints). Ask for the plan
document if you need the full detail; the short version of where things
stand:

| Phase | Scope | Status |
| --- | --- | --- |
| 0 | Toolchain + build skeleton | Skeleton files present (this commit) |
| 1 | Display + buttons + menu (first on-hardware checkpoint) | Source written, **not yet built/flashed/verified** |
| 2 | Vibration + diag/NVS (LittleFS shim) | Not started |
| 3 | C6 link (UART port + concurrency rework) | Not started |
| 4 | RFID: RC522 + RDM6300 + library | Not started |
| 5 | Wi-Fi Monitor / BT Scan background sessions | Not started |
| 6 | Infrared (PIO rewrite — highest-risk item) | Not started |
| 7 | Full regression + host tests + doc updates | Not started |

**Phase 1 is the current blocker**: nothing later should be started until
the menu renders correctly and all 6 inputs (UP/DOWN/LEFT/RIGHT/PRESS/
BACK) navigate correctly on real hardware.

## What's in this directory so far

```
pico/
  CMakeLists.txt            # root Pico SDK project file
  main/
    CMakeLists.txt           # Phase 1-scoped target; later phases' files listed commented-out
    main.c                   # Phase 1 bring-up loop: display + buttons + menu only, all other subsystems stubbed
    ui/                       # display.c (rewritten for RP2040 SPI), menu.c/text_entry.c/font8x16_basic.c (ported as-is)
    input/                    # buttons.c/h (rewritten for the module's digital joystick)
    ir/, rfid/, feedback/, net/, diag/, platform/   # headers + MCU-independent files copied ahead of their phase; .c files not yet written
```

## Before your first build

1. **Fetch `pico_sdk_import.cmake`** from your installed Pico SDK
   (`pico-sdk/external/pico_sdk_import.cmake`) into this directory — it's
   standard SDK boilerplate, not vendored in this repo.
2. **Cross-check `main/ui/display.c`'s ST7789 init sequence** against
   Waveshare's own Pico-LCD-1.3 demo source. It was written from the
   standard community/vendor ST7789 init sequence without access to that
   demo — if colors look wrong (inverted/swapped) or the image is
   mirrored/rotated, that's the first place to check (`MADCTL`/`INVON`
   values in `display_init()`).
3. **Confirm the display's SPI instance.** `display.c` assumes `spi1` for
   the Waveshare module's fixed SCK/MOSI pins (GP10/GP11); this is a
   best-effort assumption from the pin budget, not verified against the
   vendor demo yet. If wrong, `rc522.c` (Phase 4, not written yet) needs
   its SPI instance swapped to match.

## Pin plan (RP2040)

Fixed by the Waveshare Pico-LCD-1.3 module (cannot move):

| Function | GPIO |
| --- | --- |
| LCD SCK | GP10 |
| LCD MOSI | GP11 |
| LCD CS | GP9 |
| LCD DC | GP8 |
| LCD RST | GP12 |
| LCD BL | GP13 |
| Joystick UP | GP2 |
| Joystick DOWN | GP18 |
| Joystick LEFT | GP16 |
| Joystick RIGHT | GP20 |
| Joystick PRESS | GP3 |
| Button A | GP15 (unmapped — reserved for future shortcuts) |
| Button B | GP17 (mapped to `BUTTON_BACK`) |
| Button X | GP19 (unmapped — reserved for future shortcuts) |
| Button Y | GP21 (unmapped — reserved for future shortcuts) |

Remaining peripherals, placed in the 11 GPIOs left free (planned for their
respective phases, not yet wired in code):

| Peripheral | GPIO | Bus |
| --- | --- | --- |
| RC522 (Phase 4) | SCK=GP6, MOSI=GP7, MISO=GP4, CS=GP5, RST=GP22 | spi0 (pending Step 3 above) |
| C6-link UART (Phase 3) | TX=GP0, RX=GP1 | uart0 |
| RDM6300 (Phase 4) | RX=GP14 (RX-only) | uart1 |
| IR RX / VS1838B (Phase 6) | GP27 | PIO0 |
| IR TX / IR LED (Phase 6) | GP28 | PIO0 |
| Vibration motor (Phase 2) | GP26 | plain GPIO |

The C6-link's logical roles (Pico TX → C6 RX, Pico RX → C6 TX) and the
full UART wire protocol are unchanged from the P4 build — only the
Pico-side physical GPIO numbers differ. `c6-firmware/`'s own UART pins are
untouched.

## What does NOT change in this migration

- `../c6-firmware/` — completely untouched, same build, same behavior.
- The UART wire protocol to the C6 (commands, line format, 115200-8N1,
  no flow control).
- `DISPLAY_ROWS`/`DISPLAY_COLS`/`DISPLAY_WIDTH_PX`/`DISPLAY_HEIGHT_PX` —
  240x240 is already the Waveshare module's native resolution.
- The menu tree structure (`ui/menu.c/h`) and `text_entry.c`'s grid layout
  — both MCU-independent, copied over unchanged.
- `ir/ir_direction.c` stays disabled/out of scope, same as in `main/`.
- Physical A/X/Y buttons are left unpolled/unmapped in this migration.

## Testing

No Pico SDK toolchain or hardware is required for the host-side logic
tests in `../tests/` — that suite is being extended alongside this port
(new stub headers for GPIO/UART/timer, following the same
`#include`-the-real-`.c`-file pattern already used for the P4 build) but
hasn't picked up Pico-specific test files yet.
