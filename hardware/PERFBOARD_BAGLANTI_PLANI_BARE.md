# Perfboard wiring plan: bare stack, 46×18 holes (auto-generated)

Source: `hardware/perfboard/make_plan_bare.py`. Visual guide: `hardware/perfboard/perfboard_plan_bare.html`. 3D model: `hardware/perfboard/bare_stack.html`.
Hole coordinate = (x column 1–46, y row 1–18), seen from the **component (top) side**, top-left corner is (1,1). USB-C on the left, antenna on the right.
Wire length = hole distance × 2.54 mm + slack + stripping.

## Step 1 — Skeleton: sockets + 3V3/GND

- Put the C6 on two female 1x16 strips and the screen on 2x(1x20) MALE strips; solder the strips with the modules plugged in on the plate (the spacing then comes out right by itself).
- Power only: C6 3V3 → screen VSYS pin (the module is marked VSYS; Pico pin 39), plus the GND line.
- Fit the screen with its pin-1 end on the LEFT (C6/USB side); ⚠ confirm the screen orientation from the "USB" marking on the module.
- **Test:** WITHOUT power, multimeter continuity: 3V3 ↔ GND must not beep. Then plug in only the C6 over USB: does it boot?

| # | From | To | Holes | Cut | Colour |
|---:|---|---|---:|---:|---|
| 1 | J1.3V3 (x18,y2) | LCD.VSYS (x28,y6) | 10.8 | 4.5 cm | red |
| 2 | J3.GND (x18,y10) | LCD.GND (x29,y6) | 11.7 | 4.8 cm | black |

## Step 2 — Screen signals (6 wires)

- SPI + control lines: CLK, DIN, CS, DC, RST, BL.
- The wires run horizontally from the C6 to the screen (on the underside, the solder side of the plate); crossing is normal.
- **Test:** Flash the current firmware: the menu should appear on the screen.

| # | From | To | Holes | Cut | Colour |
|---:|---|---|---:|---:|---|
| 3 | J3.IO21 (x12,y10) | LCD.BL (x43,y13) | 31.1 | 10.5 cm | white |
| 4 | J3.IO9 (x8,y10) | LCD.CS (x38,y13) | 30.1 | 10.2 cm | green |
| 5 | J1.IO8 (x10,y2) | LCD.DC (x37,y13) | 29.2 | 9.9 cm | blue |
| 6 | J3.IO20 (x11,y10) | LCD.RST (x42,y13) | 31.1 | 10.5 cm | purple |
| 7 | J3.IO19 (x10,y10) | LCD.DIN (x41,y13) | 31.1 | 10.5 cm | orange |
| 8 | J3.IO18 (x9,y10) | LCD.CLK (x40,y13) | 31.1 | 10.5 cm | yellow |

## Step 3 — Joystick + A (5 wires)

- UP, DOWN, LEFT, RIGHT, A. All of them sit on the screen module, whose common GND is already connected.
- **Test:** Navigate the menu with UP/DOWN/RIGHT/LEFT/A.

| # | From | To | Holes | Cut | Colour |
|---:|---|---|---:|---:|---|
| 9 | J1.IO10 (x9,y2) | LCD.A (x46,y13) | 38.6 | 12.7 cm | orange |
| 10 | J1.IO11 (x8,y2) | LCD.DOWN (x43,y6) | 35.2 | 11.7 cm | blue |
| 11 | J3.IO23 (x14,y10) | LCD.LEFT (x46,y6) | 32.2 | 10.8 cm | light green |
| 12 | J3.IO22 (x13,y10) | LCD.RIGHT (x41,y6) | 28.3 | 9.7 cm | red |
| 13 | J1.IO6 (x14,y2) | LCD.UP (x30,y13) | 19.4 | 7.1 cm | light blue |

## Step 4 — Buzzer + vibration motor + load selector

- Put the jumper on JP1 at 1-2 (USB test, 3V3).
- IO1 goes to both the buzzer and the motor driver's base resistor.
- The banded end (cathode) of D1 faces the M+ side.
- 470Ω and 10k are not in the order: use 2x 1k in parallel (=500Ω) for Rb2. Rp (base→GND) is not required; leave it out or buy a 10k.
- **Test:** On entering a menu the buzzer should beep and you should feel the vibration.

| # | From | To | Holes | Cut | Colour |
|---:|---|---|---:|---:|---|
| 14 | LCD.VSYS (x28,y6) | JP1.3V3 (x44,y18) | 20.0 | 7.2 cm | red |
| 15 | Rb2.1 (x35,y17) | BZ1.BZ+ (x41,y17) | 6.0 | 3.2 cm | green |
| 16 | J1.IO1 (x11,y2) | Rb2.1 (x35,y17) | 28.3 | 9.7 cm | green |
| 17 | Rp2.2 (x40,y15) | Q2.E (x40,y16) | 1.0 | 1.7 cm | black |
| 18 | Q2.E (x40,y16) | BZ1.BZ− (x42,y17) | 2.2 | 2.1 cm | black |
| 19 | LCD.GND (x29,y6) | Rp2.2 (x40,y15) | 14.2 | 5.6 cm | black |
| 20 | Rb2.2 (x39,y17) | Q2.B (x40,y17) | 1.0 | 1.7 cm | gri |
| 21 | Rb2.2 (x39,y17) | Rp2.1 (x36,y15) | 3.6 | 2.5 cm | gri |
| 22 | Q2.C (x40,y18) | M1.M− (x41,y18) | 1.0 | 1.7 cm | gri |
| 23 | M1.M− (x41,y18) | D1.A (x41,y16) | 2.0 | 2.0 cm | gri |
| 24 | D1.K (x45,y16) | JP1.VLOAD (x45,y18) | 2.0 | 2.0 cm | pink |
| 25 | M1.M+ (x42,y18) | JP1.VLOAD (x45,y18) | 3.0 | 2.3 cm | pink |

## Step 5 — RC522 port (6 wires)

- SCK and MOSI are SHARED with the screen: a second wire goes to the same C6 hole. The thin RC522 wires are soldered from below to these 8 holes (top strip, row 3); the wire does not pass through the plate, it is soldered flat to the pad on the underside.
- Keep the port order the same as the RC522 module so an 8-way Dupont cable plugs in straight.
- **Test:** hardware_profile.h → BOARD_HAS_RC522 = 1, flash. In the RFID menu present a card: UID.

| # | From | To | Holes | Cut | Colour |
|---:|---|---|---:|---:|---|
| 26 | LCD.VSYS (x28,y6) | J_RC522.3.3V (x34,y3) | 6.7 | 3.4 cm | red |
| 27 | LCD.GND (x29,y6) | J_RC522.GND (x32,y3) | 4.2 | 2.6 cm | black |
| 28 | J1.IO5 (x15,y2) | J_RC522.MISO (x30,y3) | 15.0 | 5.8 cm | light blue |
| 29 | LCD.DIN (x41,y13) | J_RC522.MOSI (x29,y3) | 15.6 | 6.0 cm | orange |
| 30 | J1.IO7 (x13,y2) | J_RC522.SDA/CS (x27,y3) | 14.0 | 5.5 cm | green |
| 31 | J1.IO2 (x7,y2) | J_RC522.RST (x33,y3) | 26.0 | 9.0 cm | red |
| 32 | LCD.CLK (x40,y13) | J_RC522.SCK (x28,y3) | 15.6 | 6.0 cm | yellow |

## Step 6 — RDM6300 port + divider

- TX (5V level) → 1k → [IO15 node] → 1k+1k → GND: 5V → 3.3V.
- The RDM6300 is powered from 5V (the C6's 5V pin).
- First power the RDM6300 from 5V and measure TX with a multimeter. If it is ~5V the divider is required; if it is ~3.3V SKIP the divider (3.3V drops to 2.2V and the C6 input may not read HIGH) and feed TX straight to IO15.
- **Test:** Bring a 125kHz key fob near the coil: an ID should appear.

| # | From | To | Holes | Cut | Colour |
|---:|---|---|---:|---:|---|
| 33 | J1.5V (x5,y2) | J_RDM.5V (x29,y1) | 24.0 | 8.4 cm | orange |
| 34 | J_RC522.GND (x32,y3) | J_RDM.GND (x28,y1) | 4.5 | 2.7 cm | black |
| 35 | J_RC522.GND (x32,y3) | R9.2 (x44,y1) | 12.2 | 5.0 cm | black |
| 36 | R8.1 (x39,y1) | R9.1 (x40,y1) | 1.0 | 1.7 cm | gri |
| 37 | R7.2 (x34,y1) | R8.2 (x35,y1) | 1.0 | 1.7 cm | orange |
| 38 | J3.IO15 (x15,y10) | R7.2 (x34,y1) | 21.0 | 7.5 cm | orange |
| 39 | J_RDM.TX (x27,y1) | R7.1 (x30,y1) | 3.0 | 2.3 cm | gri |

## Step 7 — IR receiver + IR transmitter

- VS1838B: OUT → IO4.
- Transmitter: IO0 → 1k → 2N2222 base; LED to VLOAD through 33Ω.
- The transmitter you have is a KY-005 MODULE (not a bare LED). Sources disagree about the middle pin: before soldering, find the LED's two ends with the multimeter diode mode and do not connect the middle pin.
- Before soldering Q1/Q2 verify the pin order with the hFE socket (PN2222A E-B-C, P2N2222A C-B-E).
- **Test:** Capture a remote's code with IR Learn; watch the IR Send Test through a phone camera (it blinks purple).

| # | From | To | Holes | Cut | Colour |
|---:|---|---|---:|---:|---|
| 40 | J_RC522.3.3V (x34,y3) | IR_RX.VCC (x40,y2) | 6.1 | 3.2 cm | red |
| 41 | Rp1.2 (x33,y15) | Q1.E (x33,y16) | 1.0 | 1.7 cm | black |
| 42 | R9.2 (x44,y1) | IR_RX.GND (x39,y2) | 5.1 | 2.9 cm | black |
| 43 | Rp1.2 (x33,y15) | Rp2.2 (x40,y15) | 7.0 | 3.4 cm | black |
| 44 | J1.IO4 (x16,y2) | IR_RX.OUT (x38,y2) | 22.0 | 7.8 cm | blue |
| 45 | J1.IO0 (x12,y2) | Rb1.1 (x28,y17) | 21.9 | 7.8 cm | purple |
| 46 | LED1.A (x45,y17) | Rled.2 (x45,y15) | 2.0 | 2.0 cm | gri |
| 47 | Rb1.2 (x32,y17) | Q1.B (x33,y17) | 1.0 | 1.7 cm | gri |
| 48 | Rb1.2 (x32,y17) | Rp1.1 (x29,y15) | 3.6 | 2.5 cm | gri |
| 49 | Q1.C (x33,y18) | LED1.K (x44,y17) | 11.0 | 4.6 cm | gri |
| 50 | Rled.1 (x41,y15) | M1.M+ (x42,y18) | 3.2 | 2.3 cm | pink |

## Step 8 — Battery gauge + power inputs

- Battery divider (R1,R2) and the J_VBAT, J_5V ports, capacitors.
- Last: set the MT3608 to 5.3V unloaded, then connect it (docs/planning/ERDEM_SATIN_ALINACAKLAR.md).
- **Test:** The screen should show a battery percentage. Move the JP1 jumper to 2-3.

| # | From | To | Holes | Cut | Colour |
|---:|---|---|---:|---:|---|
| 51 | J_RDM.5V (x29,y1) | C2.+ (x30,y2) | 1.4 | 1.8 cm | orange |
| 52 | J_RDM.5V (x29,y1) | J_5V.5V (x27,y2) | 2.2 | 2.1 cm | orange |
| 53 | C2.+ (x30,y2) | C1.+ (x34,y2) | 4.0 | 2.6 cm | orange |
| 54 | R1.2 (x22,y7) | R2.2 (x24,y7) | 2.0 | 2.0 cm | yellow |
| 55 | R2.2 (x24,y7) | C3.1 (x26,y4) | 3.6 | 2.5 cm | yellow |
| 56 | J1.IO3 (x6,y2) | R1.2 (x22,y7) | 16.8 | 6.3 cm | yellow |
| 57 | J_RC522.GND (x32,y3) | C2.− (x32,y2) | 1.0 | 1.7 cm | black |
| 58 | J_RDM.GND (x28,y1) | J_5V.GND (x28,y2) | 1.0 | 1.7 cm | black |
| 59 | LCD.GND (x29,y6) | C3.2 (x26,y6) | 3.0 | 2.3 cm | black |
| 60 | C1.− (x36,y2) | IR_RX.GND (x39,y2) | 3.0 | 2.3 cm | black |
| 61 | R2.1 (x24,y3) | C3.2 (x26,y6) | 3.6 | 2.5 cm | black |
| 62 | J3.GND (x18,y10) | J_VBAT.GND (x23,y10) | 5.0 | 2.9 cm | black |
| 63 | J_VBAT.VBAT (x22,y10) | R1.1 (x22,y3) | 7.0 | 3.4 cm | pink |
| 64 | J_VBAT.VBAT (x22,y10) | JP1.VBAT (x46,y18) | 25.3 | 8.8 cm | pink |

## Step 9 — Power chain (between modules, off the plate)

- This step is not holes on the plate but wires between modules. Order matters: set the MT3608 first, then connect it.
- Without connecting the MT3608 to anything else, connect it to the battery, measure the output with a multimeter and set it to 5.3V. The trimmer direction can be reversed depending on the seller: turn in small steps and find the direction by measuring.
- The TP4056 must be the protected kind (it has B+/B− and also OUT+/OUT− pads). The load is taken from the OUT pads, the battery goes to the B pads.
- The TP4056 module in the order was 9.8 TL: that price suggests an unprotected module (only B+/B−), but this is unverified. If your board has no OUT+/OUT− pads, do not use it without the battery's own protection circuit (PCM); buy a protected TP4056.
- Switch the device off while charging; turn the switch off before plugging USB into the board.
- **Test:** Switch on, no USB: the screen should light up and show the battery percentage. The JP1 jumper goes to 2-3.

| # | From | To | Holes | Cut | Colour |
|---:|---|---|---:|---:|---|
| From | To | Colour | Note |
|---|---|---|---|
| LiPo (+) | TP4056 B+ | red | JST cable |
| LiPo (−) | TP4056 B− | black |  |
| TP4056 OUT+ | PPTC 1.1A → switch input | red | between the fuse and the switch |
| switch output | J_VBAT.VBAT (plate) + MT3608 VIN+ | red | this rail = battery rail after the switch (3.0–4.2V) |
| TP4056 OUT− | J_VBAT.GND (plate) + MT3608 VIN− | black |  |
| MT3608 VOUT+ | 1N5819 anode | orange | the diode prevents back-feeding USB |
| 1N5819 cathode (banded end) | J_5V.5V (plate) | orange | the 1000µF C1 is already on this rail |
| MT3608 VOUT− | J_5V.GND (plate) | black |  |
| JP1 jumper | pin 2-3 (VLOAD = VBAT) | — | it was 1-2 during the USB test |
