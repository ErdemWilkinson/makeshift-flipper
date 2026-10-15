#!/usr/bin/env python3
"""Perfboard wiring plan generator for the BARE STACK (single 46 x 18 hole plate).

Run:  python make_plan_bare.py
Out:  perfboard_plan_bare.html          (interactive step-by-step guide)
      ../PERFBOARD_BAGLANTI_PLANI_BARE.md  (same wire list as text; filename kept for old links)

Grid: 46 x 18 holes (118 x 46 mm cut from a 12x12 cm board, 2.54 mm pitch). x grows
right, y grows down, viewed from the COMPONENT (top) side. ESP32 USB-C end is LEFT,
its antenna end RIGHT (facing the screen); LCD pin 1 is on the left.
Hole (x, y) centre in mm from the plate's top-left corner: ((x-1)*2.54+1.3, (y-1)*2.54+1.3).

Wires are soldered on the underside (solder side) of the plate. TP4056 and MT3608
sit on 1 mm foam over rows 12-18 (x 1.3-26.5): no pads there. The RC522 and the RDM6300
coil/board are NOT on the plate; their wires come to the J_RC522 / J_RDM holes.
"""
import itertools
import re
import json
import math
import os
from collections import defaultdict

W, H = 46, 18
HERE = os.path.dirname(os.path.abspath(__file__))

pads, parts = [], []


def pad(ref, pin, x, y, net, stage, label=None, lab=None, forbid=False, strap=False):
    pads.append(dict(id=f"{ref}.{pin}", ref=ref, pin=str(pin), x=x, y=y, net=net,
                     stage=stage, label=label if label is not None else str(pin),
                     lab=lab, forbid=forbid, strap=strap))


def part(ref, name, kind, stage, note="", value=""):
    parts.append(dict(ref=ref, name=name, kind=kind, stage=stage, note=note, value=value))


# ---------------------------------------------------------------- ESP32-C6: two 1x16 female strips, ROW_GAP holes apart
# Row J1 on top (y=2), J3 below (y=2+ROW_GAP). Pin 1 at the antenna end (right): pin i at x = 19 - i. USB-C end is left.
ROW_GAP = 8          # rows of the C6 headers are 8 holes apart (user measured 16 x 16 pins, 8 spacing)
J1Y = 2
J3Y = J1Y + ROW_GAP
J1 = ["3V3", "RST", "IO4", "IO5", "IO6", "IO7", "IO0", "IO1", "IO8", "IO10",
      "IO11", "IO2", "IO3", "5V", "GND", "NC"]
J3 = ["GND", "TX", "RX", "IO15", "IO23", "IO22", "IO21", "IO20", "IO19", "IO18",
      "IO9", "GND", "IO13", "IO12", "GND", "NC"]
J1NET = {"3V3": "3V3", "IO4": "IR_RX", "IO5": "MISO", "IO6": "BTN_UP", "IO7": "RC_CS",
         "IO0": "IR_TX", "IO1": "FB", "IO8": "LCD_DC", "IO10": "BTN_A",
         "IO11": "BTN_DOWN", "IO2": "RC_RST", "IO3": "BAT_SENSE", "5V": "5V", "GND": "GND"}
J3NET = {"IO15": "RDM_TX", "IO23": "BTN_LEFT", "IO22": "BTN_RIGHT", "IO21": "LCD_BL",
         "IO20": "LCD_RST", "IO19": "MOSI", "IO18": "SCK", "IO9": "LCD_CS", "GND": "GND"}
FORBID = {"TX", "RX", "IO12", "IO13"}
STRAP = {"IO4", "IO5", "IO8", "IO9", "IO15"}
part("J1", "ESP32-C6 upper strip (J1)", "header", 1, "1x16 female header")
part("J3", "ESP32-C6 lower strip (J3)", "header", 1, "1x16 female header")
for i, n in enumerate(J1):
    pad("J1", i + 1, 18 - i, J1Y, J1NET.get(n), 1, n, lab="t", strap=n in STRAP)
for i, n in enumerate(J3):
    pad("J3", i + 1, 18 - i, J3Y, J3NET.get(n), 1, n, lab="b",
        forbid=n in FORBID, strap=n in STRAP)

# ---------------------------------------------------------------- Pico-LCD-1.3: 2x20 male pins, 7 holes apart
# pin 1 at the left. Pins 1-20 on the lower row (y=13), x = 26 + p; pins 21-40 on the upper row (y=6), x = 67 - p.
LROW_LOW, LROW_UP = 13, 6
PICO = {1: "GP0", 2: "GP1", 3: "GND", 4: "GP2", 5: "GP3", 6: "GP4", 7: "GP5", 8: "GND",
        9: "GP6", 10: "GP7", 11: "GP8", 12: "GP9", 13: "GND", 14: "GP10", 15: "GP11",
        16: "GP12", 17: "GP13", 18: "GND", 19: "GP14", 20: "GP15", 21: "GP16",
        22: "GP17", 23: "GND", 24: "GP18", 25: "GP19", 26: "GP20", 27: "GP21",
        28: "GND", 29: "GP22", 30: "RUN", 31: "GP26", 32: "GP27", 33: "GND", 34: "GP28",
        35: "VREF", 36: "3V3", 37: "3V3EN", 38: "GND", 39: "VSYS", 40: "VBUS"}
LCDUSE = {4: ("BTN_UP", 3, "UP"), 11: ("LCD_DC", 2, "DC"), 12: ("LCD_CS", 2, "CS"),
          14: ("SCK", 2, "CLK"), 15: ("MOSI", 2, "DIN"), 16: ("LCD_RST", 2, "RST"),
          17: ("LCD_BL", 2, "BL"), 20: ("BTN_A", 3, "A"), 21: ("BTN_LEFT", 3, "LEFT"),
          24: ("BTN_DOWN", 3, "DOWN"), 26: ("BTN_RIGHT", 3, "RIGHT"),
          39: ("3V3", 1, "VSYS"), 38: ("GND", 1, "GND")}
part("LCD", "Pico-LCD-1.3 (2x20 male pins, Pico layout)", "header", 1,
     "the screen's female socket (pre-soldered) sits on these pins; pin 1 on the LEFT (C6 side). ⚠ verify the orientation from the USB marking on the screen")
for p in range(1, 41):
    if p <= 20:
        x, y, lab = 26 + p, LROW_LOW, "b"
    else:
        x, y, lab = 67 - p, LROW_UP, "t"
    net, st, sh = LCDUSE.get(p, (None, 1, None))
    pad("LCD", p, x, y, net, st, sh if sh else PICO[p], lab=lab if sh else None)

# ---------------------------------------------------------------- top strip, rows 1-3, x 27-46 (above the LCD)
part("J_RC522", "RC522 wires (8 holes)", "header", 5,
     "RC522 order: SDA SCK MOSI MISO IRQ GND RST 3.3V — verify against the module's silkscreen. Thin wires are soldered to the RC522 pads from below")
for i, (n, net) in enumerate([("SDA/CS", "RC_CS"), ("SCK", "SCK"), ("MOSI", "MOSI"),
                              ("MISO", "MISO"), ("IRQ", None), ("GND", "GND"),
                              ("RST", "RC_RST"), ("3.3V", "3V3")]):
    pad("J_RC522", i + 1, 27 + i, 3, net, 5, n, lab="b")

part("J_RDM", "RDM6300 wires (3 holes)", "header", 6,
     "RDM6300 P1 order: TX, RX, NC, GND, VCC(5V). Three wires: TX, GND, VCC. RX stays unconnected")
for i, (n, net) in enumerate([("TX", "RDM_TXIN"), ("GND", "GND"), ("5V", "5V")]):
    pad("J_RDM", i + 1, 27 + i, 1, net, 6, n, lab="b")
for ref, x0, top, bot, val in [("R7", 30, "RDM_TXIN", "RDM_TX", "1k"),
                               ("R8", 35, "RDM_MID", "RDM_TX", "1k"),
                               ("R9", 40, "RDM_MID", "GND", "1k")]:
    part(ref, f"{ref} {val} (divider)", "res", 6, "5V→3.3V divider: 1k + (1k+1k)", val)
    if ref == "R8":      # RDM_TX end on the left (next to the end of R7), RDM_MID end on the right
        pad(ref, 2, x0, 1, bot, 6, "")
        pad(ref, 1, x0 + 4, 1, top, 6, "")
    else:
        pad(ref, 1, x0, 1, top, 6, "")
        pad(ref, 2, x0 + 4, 1, bot, 6, "")

part("J_5V", "5V input (2 holes)", "header", 8, "MT3608 output → 1N5819 → here")
pad("J_5V", 1, 27, 2, "5V", 8, "5V", lab=None)
pad("J_5V", 2, 28, 2, "GND", 8, "GND", lab=None)
part("C2", "C2 100µF (RDM6300 5V)", "cap", 8, "long leg + (5V)", "100µF")
pad("C2", 1, 30, 2, "5V", 8, "+", lab=None)
pad("C2", 2, 32, 2, "GND", 8, "−", lab=None)
part("C1", "C1 470–1000µF (5V rail)", "cap", 8, "long leg + (5V). Ø8 mm fits; a Ø10×16 1000µF does not", "470µF")
pad("C1", 1, 34, 2, "5V", 8, "+", lab=None)
pad("C1", 2, 36, 2, "GND", 8, "−", lab=None)

part("IR_RX", "VS1838B IR receiver (3 holes)", "header", 7,
     "bare VS1838B (dome facing you, from the left): OUT, GND, VCC. On a KY-022 board the order is GND, VCC, S: read the silkscreen")
for i, (n, net) in enumerate([("OUT", "IR_RX"), ("GND", "GND"), ("VCC", "3V3")]):
    pad("IR_RX", i + 1, 38 + i, 2, net, 7, n, lab=None)

# ---------------------------------------------------------------- gap columns x 22-26, rows 1-10 (between C6 and LCD)
part("J_VBAT", "Battery rail (2 holes)", "header", 8, "battery rail AFTER the switch (3.0–4.2V)")
pad("J_VBAT", 1, 22, 10, "VBAT", 8, "VBAT", lab=None)
pad("J_VBAT", 2, 23, 10, "GND", 8, "GND", lab=None)
part("R1", "R1 10k (battery divider, upper)", "res", 8, "", "10k")
pad("R1", 1, 22, 3, "VBAT", 8, "")
pad("R1", 2, 22, 7, "BAT_SENSE", 8, "")
part("R2", "R2 10k (battery divider, lower)", "res", 8, "", "10k")
pad("R2", 1, 24, 3, "GND", 8, "")
pad("R2", 2, 24, 7, "BAT_SENSE", 8, "")
part("C3", "C3 100nF (ADC filter, optional)", "cap", 8, "", "100nF")
pad("C3", 1, 26, 4, "BAT_SENSE", 8, "")
pad("C3", 2, 26, 6, "GND", 8, "")


# ---------------------------------------------------------------- bottom strip, rows 15-18, x 28-46 (below the LCD)
def nswitch_h(tag, ox, oy, stage, rb_val, in_net, qlabel):
    """Generic NPN low-side switch, laid out horizontally (6 columns x 4 rows: ox..ox+5, oy-1..oy+2)."""
    q = f"Q{tag}"
    part(f"Rb{tag}", f"Rb{tag} {rb_val} (base)", "res", stage, "", rb_val)
    pad(f"Rb{tag}", 1, ox, oy + 1, in_net, stage, "")
    pad(f"Rb{tag}", 2, ox + 4, oy + 1, f"{q}_B", stage, "")
    part(f"Rp{tag}", f"Rp{tag} 10k (base→GND)", "res", stage, "", "10k")
    pad(f"Rp{tag}", 1, ox + 1, oy - 1, f"{q}_B", stage, "")
    pad(f"Rp{tag}", 2, ox + 5, oy - 1, "GND", stage, "")
    part(q, f"{q} 2N2222 ({qlabel})", "tr", stage,
         "TO-92 pinout varies by manufacturer (PN2222A: E-B-C, P2N2222A: C-B-E). Verify with the multimeter hFE socket before soldering.")
    pad(q, "E", ox + 5, oy, "GND", stage, "E", lab=None)
    pad(q, "B", ox + 5, oy + 1, f"{q}_B", stage, "B", lab=None)
    pad(q, "C", ox + 5, oy + 2, f"{q}_C", stage, "C", lab=None)


nswitch_h("1", 28, 16, 7, "1k", "IR_TX", "IR LED driver")
nswitch_h("2", 35, 16, 4, "470Ω", "FB", "motor driver")

part("LED1", "IR LED 940nm (2 holes)", "header", 7,
     "Bare LED: short leg = cathode (K). On a KY-005 module find the LED's two ends with the multimeter diode mode (S and − pins).")
pad("LED1", "K", 44, 17, "Q1_C", 7, "K", lab=None)
pad("LED1", "A", 45, 17, "LED_A", 7, "A", lab=None)
part("Rled", "Rled 33Ω (LED series)", "res", 7,
     "33Ω = 3x 100Ω in parallel (from the ordered 100R pack). If the KY-005 has its own resistor, make this one smaller.", "33Ω")
pad("Rled", 1, 41, 15, "VLOAD", 7, "")
pad("Rled", 2, 45, 15, "LED_A", 7, "")
part("M1", "Vibration motor (2 holes)", "header", 4, "motor is non-polar; put the wire ends straight into the holes")
pad("M1", "−", 41, 18, "Q2_C", 4, "M−", lab=None)
pad("M1", "+", 42, 18, "VLOAD", 4, "M+", lab=None)
part("D1", "D1 1N5819 (reverse/flyback)", "diode", 4, "BANDED end = cathode → VLOAD (M+)")
pad("D1", "A", 41, 16, "Q2_C", 4, "")
pad("D1", "K", 45, 16, "VLOAD", 4, "")
part("BZ1", "Active buzzer (2 holes)", "header", 4, "+ leg (long) → IO1, − → GND")
pad("BZ1", "+", 41, 17, "FB", 4, "BZ+", lab=None)
pad("BZ1", "−", 42, 17, "GND", 4, "BZ−", lab=None)
part("JP1", "Load selector JP1 (1x3 male + jumper)", "header", 4,
     "jumper 1-2 = USB test (3V3), 2-3 = battery (VBAT)")
pad("JP1", 1, 44, 18, "3V3", 4, "3V3", lab=None)
pad("JP1", 2, 45, 18, "VLOAD", 4, "VLOAD", lab=None)
pad("JP1", 3, 46, 18, "VBAT", 4, "VBAT", lab=None)

# ---------------------------------------------------------------- module bodies / zones (hole units)
MODULES = [
    dict(name="ESP32-C6 DevKit (top view)", x0=0.3, y0=J1Y - 0.5, x1=20.7, y1=J3Y + 0.5,
         note="USB-C ← · antenna →"),
    dict(name="Pico-LCD-1.3", x0=26.4, y0=4.3, x1=46.5, y1=14.8, note="pin 1 on the left"),
    dict(name="TP4056 (on 1 mm foam)", x0=1.3, y0=11.7, x1=11.5, y1=18.4, note="no pads"),
    dict(name="MT3608 (on 1 mm foam)", x0=11.9, y0=11.7, x1=26.5, y1=18.4, note="no pads"),
]
ZONES = [
    dict(name="ANTENNA: no wires/parts", x0=17.8, y0=0.5, x1=21.6, y1=11.7, kind="keepout"),
]

STAGES = [
    dict(n=1, title="Skeleton: sockets + 3V3/GND",
         intro=["Put the C6 on two female 1x16 strips and the screen on 2x(1x20) MALE strips; solder the strips with the modules plugged in on the plate (the spacing then comes out right by itself).",
                "Power only: C6 3V3 → screen VSYS pin (the module is marked VSYS; Pico pin 39), plus the GND line.",
                "Fit the screen with its pin-1 end on the LEFT (C6/USB side); ⚠ confirm the screen orientation from the \"USB\" marking on the module."],
         test="WITHOUT power, multimeter continuity: 3V3 ↔ GND must not beep. Then plug in only the C6 over USB: does it boot?"),
    dict(n=2, title="Screen signals (6 wires)",
         intro=["SPI + control lines: CLK, DIN, CS, DC, RST, BL.",
                "The wires run horizontally from the C6 to the screen (on the underside, the solder side of the plate); crossing is normal."],
         test="Flash the current firmware: the menu should appear on the screen."),
    dict(n=3, title="Joystick + A (5 wires)",
         intro=["UP, DOWN, LEFT, RIGHT, A. All of them sit on the screen module, whose common GND is already connected."],
         test="Navigate the menu with UP/DOWN/RIGHT/LEFT/A."),
    dict(n=4, title="Buzzer + vibration motor + load selector",
         intro=["Put the jumper on JP1 at 1-2 (USB test, 3V3).",
                "IO1 goes to both the buzzer and the motor driver's base resistor.",
                "The banded end (cathode) of D1 faces the M+ side.",
                "470Ω and 10k are not in the order: use 2x 1k in parallel (=500Ω) for Rb2. Rp (base→GND) is not required; leave it out or buy a 10k."],
         test="On entering a menu the buzzer should beep and you should feel the vibration."),
    dict(n=5, title="RC522 port (6 wires)",
         intro=["SCK and MOSI are SHARED with the screen: a second wire goes to the same C6 hole. The thin RC522 wires are soldered from below to these 8 holes (top strip, row 3); the wire does not pass through the plate, it is soldered flat to the pad on the underside.",
                "Keep the port order the same as the RC522 module so an 8-way Dupont cable plugs in straight."],
         test="hardware_profile.h → BOARD_HAS_RC522 = 1, flash. In the RFID menu present a card: UID."),
    dict(n=6, title="RDM6300 port + divider",
         intro=["TX (5V level) → 1k → [IO15 node] → 1k+1k → GND: 5V → 3.3V.",
                "The RDM6300 is powered from 5V (the C6's 5V pin).",
                "First power the RDM6300 from 5V and measure TX with a multimeter. If it is ~5V the divider is required; if it is ~3.3V SKIP the divider (3.3V drops to 2.2V and the C6 input may not read HIGH) and feed TX straight to IO15."],
         test="Bring a 125kHz key fob near the coil: an ID should appear."),
    dict(n=7, title="IR receiver + IR transmitter",
         intro=["VS1838B: OUT → IO4.",
                "Transmitter: IO0 → 1k → 2N2222 base; LED to VLOAD through 33Ω.",
                "The transmitter you have is a KY-005 MODULE (not a bare LED). Sources disagree about the middle pin: before soldering, find the LED's two ends with the multimeter diode mode and do not connect the middle pin.",
                "Before soldering Q1/Q2 verify the pin order with the hFE socket (PN2222A E-B-C, P2N2222A C-B-E)."],
         test="Capture a remote's code with IR Learn; watch the IR Send Test through a phone camera (it blinks purple)."),
    dict(n=8, title="Battery gauge + power inputs",
         intro=["Battery divider (R1,R2) and the J_VBAT, J_5V ports, capacitors.",
                "Last: set the MT3608 to 5.3V unloaded, then connect it (docs/planning/ERDEM_SATIN_ALINACAKLAR.md)."],
         test="The screen should show a battery percentage. Move the JP1 jumper to 2-3."),

    dict(n=9, title="Power chain (between modules, off the plate)",
         intro=["This step is not holes on the plate but wires between modules. Order matters: set the MT3608 first, then connect it.",
                "Without connecting the MT3608 to anything else, connect it to the battery, measure the output with a multimeter and set it to 5.3V. The trimmer direction can be reversed depending on the seller: turn in small steps and find the direction by measuring.",
                "The TP4056 must be the protected kind (it has B+/B− and also OUT+/OUT− pads). The load is taken from the OUT pads, the battery goes to the B pads.",
                "The TP4056 module in the order was 9.8 TL: that price suggests an unprotected module (only B+/B−), but this is unverified. If your board has no OUT+/OUT− pads, do not use it without the battery's own protection circuit (PCM); buy a protected TP4056.",
                "Switch the device off while charging; turn the switch off before plugging USB into the board."],
         test="Switch on, no USB: the screen should light up and show the battery percentage. The JP1 jumper goes to 2-3."),
]
HARNESS = [
    ("LiPo (+)", "TP4056 B+", "red", "JST cable"),
    ("LiPo (−)", "TP4056 B−", "black", ""),
    ("TP4056 OUT+", "PPTC 1.1A → switch input", "red", "between the fuse and the switch"),
    ("switch output", "J_VBAT.VBAT (plate) + MT3608 VIN+", "red", "this rail = battery rail after the switch (3.0–4.2V)"),
    ("TP4056 OUT−", "J_VBAT.GND (plate) + MT3608 VIN−", "black", ""),
    ("MT3608 VOUT+", "1N5819 anode", "orange", "the diode prevents back-feeding USB"),
    ("1N5819 cathode (banded end)", "J_5V.5V (plate)", "orange", "the 1000µF C1 is already on this rail"),
    ("MT3608 VOUT−", "J_5V.GND (plate)", "black", ""),
    ("JP1 jumper", "pin 2-3 (VLOAD = VBAT)", "—", "it was 1-2 during the USB test"),
]

# --------------------------------------------------------------------------
# Wires: per net, per stage, Kruskal with C6 same-net pads pre-joined
# --------------------------------------------------------------------------
NETCOL = {  # net -> (hex, colour suggestion)
    "3V3": ("#ff4d4d", "red"), "5V": ("#ff9a1f", "orange"), "GND": ("#cfd8dc", "black"),
    "VBAT": ("#ff5fa2", "pink"), "VLOAD": ("#ff8fb8", "pink"),
    "SCK": ("#ffd21a", "yellow"), "MOSI": ("#ff9f1c", "orange"), "MISO": ("#1fd6e6", "light blue"),
    "LCD_CS": ("#27d651", "green"), "LCD_DC": ("#3d7bff", "blue"), "LCD_RST": ("#a24dff", "purple"),
    "LCD_BL": ("#f0f3f8", "white"), "BTN_UP": ("#5ec8ff", "light blue"), "BTN_DOWN": ("#5e8cff", "blue"),
    "BTN_LEFT": ("#9ad66a", "light green"), "BTN_RIGHT": ("#ff7b7b", "red"),
    "BTN_A": ("#ffb36b", "orange"), "RC_CS": ("#26a69a", "green"), "RC_RST": ("#ef5350", "red"),
    "RDM_TX": ("#ff6f00", "orange"), "IR_RX": ("#42a5f5", "blue"), "IR_TX": ("#ab47bc", "purple"),
    "FB": ("#66bb6a", "green"), "BAT_SENSE": ("#ffee58", "yellow"),
}
DEFCOL = ("#b0bec5", "gri")


def dist(a, b):
    return math.hypot(a["x"] - b["x"], a["y"] - b["y"])


def route(a, b):
    """The wire runs freely on the plate's UNDERSIDE (solder side): a straight line."""
    pts = [(a["x"], a["y"]), (b["x"], b["y"])]
    length = math.hypot(a["x"] - b["x"], a["y"] - b["y"])
    return pts, length


def build_wires():
    by_net = defaultdict(list)
    for p in pads:
        if p["net"]:
            by_net[p["net"]].append(p)
    wires = []
    for net, ps in sorted(by_net.items()):
        parent = {p["id"]: p["id"] for p in ps}

        def find(a):
            while parent[a] != a:
                parent[a] = parent[parent[a]]
                a = parent[a]
            return a

        c6 = [p for p in ps if p["ref"] in ("J1", "J3")]
        for p in c6[1:]:  # same-net C6 pads are already tied on the module
            parent[find(p["id"])] = find(c6[0]["id"])
        edges = defaultdict(list)
        for a, b in itertools.combinations(ps, 2):
            edges[max(a["stage"], b["stage"])].append((dist(a, b), a, b))
        for st in sorted(edges):
            for d, a, b in sorted(edges[st], key=lambda e: e[0]):
                if find(a["id"]) != find(b["id"]):
                    parent[find(a["id"])] = find(b["id"])
                    col, cname = NETCOL.get(net, DEFCOL)
                    pts, length = route(a, b)
                    cut = round((length * 2.54 * 1.15 + 14) / 10, 1)  # slack + 7 mm strip each end
                    wires.append(dict(net=net, stage=st, a=a["id"], b=b["id"],
                                      ax=a["x"], ay=a["y"], bx=b["x"], by=b["y"], pts=pts,
                                      holes=round(length, 1), cut_cm=cut, color=col,
                                      cname=cname, short=d <= 1.5))
    wires.sort(key=lambda w: (w["stage"], w["net"]))
    for i, w in enumerate(wires, 1):
        w["n"] = i
    return wires


def checks():
    seen = {}
    for p in pads:
        k = (p["x"], p["y"])
        assert k not in seen, f"pad overlap {p['id']} vs {seen[k]} at {k}"
        seen[k] = p["id"]
        assert 1 <= p["x"] <= W and 1 <= p["y"] <= H, f"off board: {p['id']} {k}"
    owner = {"ESP32-C6 DevKit": ("J1", "J3"), "Pico-LCD-1.3": ("LCD",)}
    for m in MODULES:
        refs = next((v for k, v in owner.items() if m["name"].startswith(k)), ())
        for p in pads:
            if p["ref"] in refs:
                continue
            inside = m["x0"] < p["x"] < m["x1"] and m["y0"] < p["y"] < m["y1"]
            assert not inside, f"{p['id']} is inside the body of {m['name']}"
    # a resistor/diode body covers the holes between its two ends
    for part_ in parts:
        if part_["kind"] not in ("res", "diode"):
            continue
        ends = [p for p in pads if p["ref"] == part_["ref"]]
        if len(ends) != 2:
            continue
        a, b = ends
        assert a["x"] == b["x"] or a["y"] == b["y"], f"{part_['ref']} is diagonal"
        for t in range(1, int(max(abs(a["x"] - b["x"]), abs(a["y"] - b["y"])))):
            cx = a["x"] + (b["x"] - a["x"]) * t // max(abs(a["x"] - b["x"]), 1) if a["x"] != b["x"] else a["x"]
            cy = a["y"] + (b["y"] - a["y"]) * t // max(abs(a["y"] - b["y"]), 1) if a["y"] != b["y"] else a["y"]
            assert (cx, cy) not in seen, f"{part_['ref']} body is over {seen[(cx, cy)]} at ({cx},{cy})"


def main():
    checks()
    wires = build_wires()
    data = dict(W=W, H=H, pads=pads, parts=parts, wires=wires, stages=STAGES, harness=HARNESS,
                modules=MODULES, zones=ZONES, netcol={k: v[0] for k, v in NETCOL.items()})
    with open(os.path.join(HERE, "template_bare.html"), encoding="utf-8") as f:
        tpl = f.read()
    html = tpl.replace("/*__DATA__*/null", json.dumps(data, ensure_ascii=False))
    with open(os.path.join(HERE, "perfboard_plan_bare.html"), "w", encoding="utf-8") as f:
        f.write(html)
    write_markdown(wires)
    embed_3d(wires)
    per = defaultdict(int)
    for w in wires:
        per[w["stage"]] += 1
    print("wires per stage:", dict(per), "total", len(wires))
    print("longest:", max(wires, key=lambda w: w["holes"])["a"], "->",
          max(wires, key=lambda w: w["holes"])["b"], max(w["holes"] for w in wires), "holes")


def embed_3d(wires):
    """Write pad + wire coordinates into bare_stack.html (between /*PLAN*/ and /*END*/)."""
    path = os.path.join(HERE, "bare_stack.html")
    if not os.path.exists(path):
        return
    plan = dict(
        pads=[[p["ref"], p["label"] or p["pin"], p["x"], p["y"], p["net"], bool(p["forbid"])]
              for p in pads if p["net"] or p["forbid"]],
        wires=[[w["ax"], w["ay"], w["bx"], w["by"], w["net"], w["n"]] for w in wires],
        netcol={k: v[0] for k, v in NETCOL.items()}, W=W, H=H, rowGap=ROW_GAP)
    with open(path, encoding="utf-8") as f:
        s = f.read()
    blob = json.dumps(plan, ensure_ascii=False, separators=(",", ":"))
    s, n = re.subn(r"/\*PLAN\*/.*?/\*END\*/", lambda m: "/*PLAN*/" + blob + "/*END*/", s, flags=re.S)
    if n:
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(s)


def pad_label(pid):
    p = next(q for q in pads if q["id"] == pid)
    return f"{p['ref']}.{p['label'] or p['pin']}"


def write_markdown(wires):
    L = ["# Perfboard wiring plan: bare stack, 46×18 holes (auto-generated)", "",
         "Source: `hardware/perfboard/make_plan_bare.py`. Visual guide: `hardware/perfboard/perfboard_plan_bare.html`. 3D model: `hardware/perfboard/bare_stack.html`.",
         "Hole coordinate = (x column 1–46, y row 1–18), seen from the **component (top) side**, top-left corner is (1,1). USB-C on the left, antenna on the right.",
         "Wire length = hole distance × 2.54 mm + slack + stripping.", ""]
    for st in STAGES:
        L += [f"## Step {st['n']} — {st['title']}", ""]
        L += [f"- {t}" for t in st["intro"]]
        L += [f"- **Test:** {st['test']}", "", "| # | From | To | Holes | Cut | Colour |", "|---:|---|---|---:|---:|---|"]
        for w in [w for w in wires if w["stage"] == st["n"]]:
            L.append(f"| {w['n']} | {pad_label(w['a'])} (x{w['ax']},y{w['ay']}) | "
                     f"{pad_label(w['b'])} (x{w['bx']},y{w['by']}) | {w['holes']} | {w['cut_cm']} cm | {w['cname']} |")
        if st["n"] == 9:
            L += ["| From | To | Colour | Note |", "|---|---|---|---|"]
            L += [f"| {a} | {b} | {c} | {d} |" for a, b, c, d in HARNESS]
        L.append("")
    with open(os.path.join(HERE, "..", "PERFBOARD_BAGLANTI_PLANI_BARE.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(L))


if __name__ == "__main__":
    main()
