# MCU Architecture Decision: field unit brain (C6-standalone vs Pico+C6)

> Revision 3. Revision 1 recommended Pico+C6. Revision 2 then assumed the
> wrong C6 carrier. The invoice SKU and official schematic now identify
> the board as Waveshare ESP32-C6-Pico (4MB). Revision 1 also contained a
> factual error (it called the RP2040 single-core; it is dual-core
> Cortex-M0+), overstated several C6-standalone risks, and underweighted
> porting cost. The recommendation has flipped to **C6-standalone**. The
> corrections are listed at the end so reviewers can check them.

## Goal

Split the project into two independent devices:

- **Lab unit**: ESP32-P4-Pico running TinyML (OCR/ASR). Stationary, not
  taken into the field.
- **Field unit**: portable device for experimental (authorized/lab)
  RFID, IR, Wi-Fi and BLE work, with its own screen and buttons.

The P4 goes to the lab unit, so the field unit needs a new brain. That
leaves two real options. Keeping the P4 in the field unit contradicts the
goal and is out of scope here.

- **(a) C6-standalone**: the ESP32-C6 runs everything: UI, RFID, IR and
  its own Wi-Fi/BLE. One board, no inter-chip link.
- **(b) Pico + C6**: a Raspberry Pi Pico (RP2040) runs UI/RFID/IR. The C6
  stays a radio-only companion over the existing UART line protocol.

## Hardware on hand

RDM6300 (125kHz), RC522 (13.56MHz), 3.3V-5V logic level converter, 6x14mm
vibration motor, 3.7V 1000mAh LiPo (bare leads), TP4056 charger (Type-C),
Waveshare Pico-LCD-1.3 (240x240 ST7789 SPI, digital 5-way joystick +
A/B/X/Y), ESP32-P4-Pico, SSD1306 0.96" OLED, **Waveshare ESP32-C6-Pico
with ESP32-C6-MINI-1 and 4MB flash**, M-F and F-F jumpers, and an Arduino
Uno starter kit. The invoice product code `2504020021` identifies the C6
board; it is not an ESP32-C6-DevKitC-1.

## Current codebase facts

- `main/` (P4 firmware): ~4,700 LOC, ESP-IDF. Uses `esp_lcd` (ST7789),
  `spi_master`, `uart`, RMT (IR), `adc_oneshot` (joystick), NVS.
- `c6-firmware/`: ~1,400 LOC, ESP-IDF, Wi-Fi + NimBLE + HTTP. Measured
  linked size **~1.37MB**; already runs on a 1.5MB "large" app
  partition with 4MB flash assumed.
- `pico/`: Pico port in progress. Phase 1 of 8 (display + buttons +
  menu) source is written, ~1,740 LOC, **never compiled or flashed**.
  Phases 2-7 (NVS→LittleFS, C6 link concurrency rework, RFID, IR via PIO)
  have not started.

## The deciding factor: porting cost

Every driver API that `main/` uses (`esp_lcd`, `spi_master`, `uart`, RMT,
`adc_oneshot`, NVS, FreeRTOS) also exists on the ESP32-C6 in the same
ESP-IDF. So option (a) is mostly a **re-target**:

- Change pin `#define`s.
- Share one SPI bus between display and RC522 (see below).
- Replace `c6_link.c`'s UART transport with direct in-process calls into
  the C6's existing `wifi_commands`/`wifi_monitor`/`bt_scan` functions.
  `c6_link.h`'s API can stay, so `main.c` barely changes.
- Merge the two build trees into one ESP-IDF project.

Option (b) is a **re-platform**. It needs a new toolchain and build
system. It also rewrites the display driver (a hand-rolled ST7789 init
sequence, since `esp_lcd` doesn't exist there), replaces NVS with a
LittleFS shim, and reworks the FreeRTOS tasks in `c6_link.c` into a
cooperative state machine. IR has to be reimplemented from scratch on PIO
because the RP2040 has no RMT; this is the highest-risk item. On top of
that, (b) keeps two boards, two power domains and a UART link to debug.

| | (a) C6-standalone | (b) Pico + C6 |
|---|---|---|
| Toolchain | Same (ESP-IDF) | New (Pico SDK) |
| Code touched | Pins, SPI sharing, c6_link transport | ~4,200 LOC ported/rewritten |
| IR | RMT code reused as-is | PIO rewrite (novel, highest risk) |
| Storage | NVS as-is | LittleFS + shim |
| Boards to power/wire | 1 | 2 + UART link |
| Rough effort (hobby pace, estimate) | ~2-4 weekends | ~6-10 weekends |
| Work already done | none | Phase 1 source, unverified |

The effort figures are order-of-magnitude estimates, not measurements.
Even so, the gap between the two is large and structural.

## Option (a) in detail

### SPI: the C6 has only one general-purpose SPI host

SPI0/1 serve the flash; only SPI2 is free. The display and the RC522
therefore **share one bus** (SCK/MOSI/MISO) with separate CS lines. ESP-IDF
supports this natively, with a per-device clock (display ~40MHz, RC522
≤10MHz). Consequence: one full-frame flush (115,200 bytes at 40MHz ≈
23ms) holds the bus, and RC522 transactions wait behind it. That is
harmless for card polling. It also saves 2 pins.

### Pin budget (using the Waveshare LCD's buttons via jumpers)

| Group | Pins |
|---|---|
| Shared SPI (SCK, MOSI, MISO) | 3 |
| Display CS, DC, RST, BL | 4 (BL can be tied to 3V3 → 3) |
| RC522 CS, RST | 2 (RST can be tied high if soft reset suffices → 1) |
| RDM6300 RX | 1 |
| IR RX, IR TX | 2 |
| Joystick 5-way + BACK (button B) | 6 |
| Vibration | 1 |
| **Total with the shared SPI bus** | **19** |
| **Total with BL and RC522 RST tied off** | **17** |

The supported reference board is the exact board on hand: **Waveshare
ESP32-C6-Pico, ESP32-C6-MINI-1, 4MB flash**. It can plug directly into
the Pico-LCD-1.3. The LCD fixes these connections:

| Function | Pico signal | C6 GPIO / source |
|---|---|---:|
| LCD SCK / MOSI | GP10 / GP11 | GPIO18 / GPIO19 |
| LCD CS / DC | GP9 / GP8 | GPIO9 / GPIO8 |
| LCD RST / BL | GP12 / GP13 | GPIO20 / GPIO21 |
| Joystick UP / PRESS | GP2 / GP3 | GPIO4 / GPIO5 |
| Joystick DOWN / LEFT | GP18 / GP16 | TCA9554 IO3 / IO5 |
| Joystick RIGHT | GP20 | GPIO22 / onboard I2C SDA |
| Button A / B / X | GP15 / GP17 / GP19 | TCA9554 IO7 / IO4 / IO2 |
| Button Y | GP21 | GPIO23 / onboard I2C SCL |

GPIO22/23 are also the board's internal TCA9554 I2C bus. RIGHT holds SDA
low and Y holds SCL low while pressed. Firmware must read RIGHT directly,
skip TCA9554 transactions while RIGHT is held, recover I2C after release,
and leave Y unused. This is the main board-specific acceptance test.

External modules use the remaining header signals:

| Function | C6 GPIO | Pico signal | Note |
|---|---:|---|---|
| RC522 SCK / MOSI | 18 / 19 | GP10 / GP11 | Shared with LCD SPI |
| RC522 MISO / CS | 6 / 7 | GP4 / GP5 | Dedicated MISO and CS |
| RC522 RST | 2 | GP27 | Hardware reset |
| RDM6300 RX | 16 | GP0 | Through 5V-to-3.3V level shifter |
| IR receiver / transmitter | 14 / 17 | GP6 / GP1 | RMT RX / driver input |
| Vibration driver | 3 | GP26 | Never drive motor directly |
| Spare | 15 / 1 | GP7 / GP28 | GPIO15 is a strap pin; leave unloaded |

GPIO4, GPIO5, GPIO8 and GPIO9 are unavoidable strapping pins in the
stacked LCD map. Do not hold UP or PRESS during reset, and include repeated
cold-boot/reset tests with the LCD attached. Moving IR TX to GPIO17 keeps
the avoidable external driver load off GPIO15.

GPIO6/7 become free when the former P4-to-C6 UART transport is removed.
Native USB remains untouched. A stackable Pico header, Pico carrier, or
equivalent breakout is required for a durable build because direct LCD
stacking hides the module pins. Breadboard wiring is sufficient for the
prototype.

### RAM / flash

- RAM: framebuffer 115KB + Wi-Fi ~40-70KB + NimBLE ~20-40KB + ~5KB other
  ≈ 180-230KB of 512KB. Comfortable.
- Flash: 1.37MB today + estimated +100-250KB for UI/RFID/IR/diag ≈
  1.5-1.65MB. This overflows the current 1.5MB partition, so it needs a
  custom partition CSV. On the actual 4MB board, dual OTA slots leave
  little growth margin. Start with one large factory app partition and
  make OTA a later, measured decision.

### Concurrency (downgraded from "main risk" to "manageable")

The C6 has one 160MHz high-performance core plus a low-power core.
ESP-IDF routinely runs Wi-Fi/BLE alongside SPI displays on single-core
parts (C3/C6-based products do this). The concerns are smaller than
Revision 1 suggested:

- Display flush is SPI DMA, not CPU-bound.
- RDM6300 is 9600 baud into a hardware FIFO plus a driver ring buffer, so
  byte loss is not a realistic concern.

The one real hotspot is **Wi-Fi Monitor (promiscuous mode) under heavy
traffic plus UI redraw**. It needs sensible task priorities and should be
tested on hardware. It is a tuning task, not a blocker.

### ADC

This is not an issue. The C6's ADC1 has 7 channels (GPIO0-6), which is
enough for an analog joystick. It is also moot if the Waveshare LCD's
digital buttons are used.

## Option (b) in detail (unchanged facts, corrected framing)

- RP2040: **dual-core** Cortex-M0+ 133MHz, 264KB SRAM, 2MB flash, 26
  GPIO, 2 SPI, 2 UART, 2 PIO blocks.
- Pin fit: the Pico-LCD-1.3 fixes 15 GPIO. The remaining 11 signals land
  on exactly the 11 free pins, with **zero slack**. Both UARTs are used,
  so debugging must go over USB stdio.
- Mechanical: the LCD is a HAT that the Pico plugs into, so the Pico's
  other pins sit under/behind it. Wiring RC522/RDM6300/IR/C6 needs
  stacking headers or wires soldered to the Pico pads.
- Its strength: radio work is physically isolated on the C6. But (a)'s
  concurrency risk turned out small, so this advantage no longer
  outweighs the porting cost.

## Hardware gaps that apply to either option

These are independent of the MCU choice and must be solved regardless:

1. **RDM6300 needs a 5V supply**, and the LiPo gives 3.0-4.2V. A 5V boost
   converter (e.g. MT3608 or a 5V boost module) is needed. It is not in
   the inventory. The RDM6300's 5V TX goes through the logic level
   converter (on hand).
2. **Powering this C6-Pico**: its onboard MP28164 buck-boost accepts a
   protected 1-cell LiPo on `VSYS`. Feed the C6-Pico directly from the
   protected battery branch; use a separate 5V boost only for RDM6300.
3. **Battery protection**: confirm that either the TP4056 module
   (DW01/8205A variant, 6 pads) or the LiPo itself (protection PCB under
   the tab) provides over-discharge protection.
4. **IR parts**: the starter kit includes an IR receiver/remote set, but
   confirm whether it includes a loose 940nm IR LED. A visible LED is not
   a substitute.
5. **Motor and IR drivers**: add a logic-level MOSFET or suitable NPN and
   a flyback diode for the motor. One BC547 is marginal for an unknown
   motor stall current and cannot drive both motor and IR LED.
6. **Charging while running**: a basic TP4056 is not a load-sharing power
   path. Add a power-path board or require main power off while charging.

## Recommendation

**(a) C6-standalone on the Waveshare ESP32-C6-Pico (4MB)**, using the
Pico-LCD-1.3 as display and controls. The inventory, power tree, missing
parts and assembly order are in `C6_STANDALONE_HARDWARE_PLAN.md`.

Validate it cheaply before committing, with a one-weekend spike:

1. Verify the physical label says ESP32-C6-Pico and confirm 4MB with
   `esptool.py flash_id`.
2. Build `main/`'s display + menu + buttons for `esp32c6` using the fixed
   table above.
3. If the menu renders and navigates on real hardware, (a) is confirmed.
   Proceed with RFID → IR → merge Wi-Fi/BLE → Wi-Fi Monitor stress test.

Fall back to (b) only if the spike exposes a hard blocker, such as too
few pins on the actual board. The `pico/` Phase 1 work stays in the repo
untouched until the spike decides.

## Corrections from Revision 1

| Rev 1 claim | Correction |
|---|---|
| Pico is single-core | RP2040 is dual-core Cortex-M0+ |
| C6 single core is the "main risk" (UI stalls, RDM6300 byte loss) | DMA SPI + 9600-baud FIFO UART make this small; only heavy promiscuous-mode traffic needs tuning |
| C6 flash overflow forecloses OTA | 4MB can run the merged app, but OTA growth margin must be measured |
| C6 ADC is a constraint | ADC1 has 7 channels; fine (and moot with digital buttons) |
| Pin budget 19 of ~20-22, ~no margin | Actual C6-Pico map shares SPI; GPIO1 remains usable and GPIO15 stays unloaded as a strap pin |
| Porting cost not compared | (a) is a re-target within ESP-IDF; (b) is a re-platform. This is the deciding factor |
| Power path not discussed | C6-Pico accepts LiPo on VSYS; only RDM6300 needs a 5V boost branch |

## Open questions for reviewers

- Is there a C6-specific reason (errata, RMT channel limits: 2 TX + 2 RX,
  SPI2 sharing with DMA) that breaks option (a) that this analysis
  missed?
- Does the merged image retain enough margin in a custom 4MB single-app
  partition, and is OTA still worth reserving space for?
- Can the RIGHT/SDA firmware rule recover the onboard TCA9554 reliably
  after repeated RIGHT presses under UI load?
- Are Wi-Fi promiscuous-mode monitoring and a 240x240 UI at a reasonable
  refresh rate known to coexist well on a single C6 HP core in practice?
