#!/usr/bin/env python3
"""2D top-view drawing of the bare-stack plate (46 x 18 holes), generated from make_plan_bare.py.

Run:  python make_board_drawing.py
Out:  bare_board_top.svg         parts + pads, nets coloured
      bare_board_top_wires.svg   same plus the numbered underside wires (drawn through the board)

Everything comes from make_plan_bare.py (pads, parts, module bodies), so changing a hole
coordinate there and re-running both scripts keeps the drawing and the plan in sync.
Items that hang below the plate (RC522, coil, battery, RDM6300 board, bridge feet) are drawn
dashed from the same mm positions as bare_stack.html.
"""
import importlib.util
import os
import sys
from xml.sax.saxutils import escape

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("make_plan_bare", os.path.join(HERE, "make_plan_bare.py"))
mp = importlib.util.module_from_spec(spec)
sys.modules["make_plan_bare"] = mp
spec.loader.exec_module(mp)

S = 34                      # px per hole
MX, MT, MB = 70, 120, 330   # left/right margin, top margin, bottom margin (legend)
W, H = mp.W, mp.H
mm2h = lambda v: (v - 1.3) / 2.54 + 1.0          # mm from plate corner -> hole units
X = lambda hx: MX + (hx - 0.5) * S
Y = lambda hy: MT + (hy - 0.5) * S
SVG_W = int(MX * 2 + W * S)
SVG_H = int(MT + H * S + MB)

SHORT = {"SDA/CS": "CS", "MOSI": "MO", "MISO": "MI", "3.3V": "3V3", "RIGHT": "RGT", "DOWN": "DWN",
         "LEFT": "LFT", "VLOAD": "LD", "VBAT": "BAT", "M−": "M-", "BZ−": "BZ-", "−": "-"}


def short(label):
    if label.startswith("IO") and label[2:].isdigit():
        return label[2:]
    return SHORT.get(label, label)


def netcol(net):
    return mp.NETCOL.get(net, mp.DEFCOL)[0]


def build(with_wires):
    o = []
    add = o.append
    add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{SVG_W}" height="{SVG_H}" viewBox="0 0 {SVG_W} {SVG_H}" '
        f'font-family="Segoe UI, Arial, sans-serif">')
    add(f'<rect width="{SVG_W}" height="{SVG_H}" fill="#fbfbf8"/>')
    add(f'<text x="{MX}" y="40" font-size="26" font-weight="700" fill="#1b2430">Makeshift Flipper · bare stack · single 118×46 mm plate, top (component) view</text>')
    sub = "46×18 holes, 2.54 mm pitch · USB-C on the left, antenna on the right · hole (x, y): x = 1–46 left→right, y = 1–18 top→bottom"
    add(f'<text x="{MX}" y="68" font-size="15" fill="#4a5568">{escape(sub)}</text>')
    add(f'<text x="{MX}" y="92" font-size="14" fill="#4a5568">'
        + ("Thin coloured lines = wires soldered on the UNDERSIDE of the plate (seen through the board). " if with_wires else "")
        + 'Dashed = hangs below the plate. Hole labels: IO number, signal name; filled colour = net.</text>')

    # plate + holes
    px0, py0, px1, py1 = X(mm2h(0)), Y(mm2h(0)), X(mm2h(118)), Y(mm2h(46))
    add(f'<rect x="{px0:.1f}" y="{py0:.1f}" width="{px1 - px0:.1f}" height="{py1 - py0:.1f}" rx="6" fill="#d6e6da" stroke="#4f7a5e" stroke-width="2.5"/>')
    for hx in range(1, W + 1):
        for hy in range(1, H + 1):
            add(f'<circle cx="{X(hx):.1f}" cy="{Y(hy):.1f}" r="2.1" fill="#a9c4b1"/>')
    for hx in [1] + list(range(5, W + 1, 5)) + [W]:
        add(f'<text x="{X(hx):.1f}" y="{MT - 12}" font-size="12" text-anchor="middle" fill="#6b7a70">{hx}</text>')
    for hy in [1, 5, 10, 15, H]:
        add(f'<text x="{MX - 18}" y="{Y(hy) + 4:.1f}" font-size="12" text-anchor="middle" fill="#6b7a70">{hy}</text>')

    # hanging items (dashed), from bare_stack.html mm positions
    def hang(x0, y0, dx, dy, label, stroke, fill, lx=None, ly=None, size=12, weight="600", anchor="start"):
        a, b, c, d = X(mm2h(x0)), Y(mm2h(y0)), X(mm2h(x0 + dx)), Y(mm2h(y0 + dy))
        add(f'<rect x="{a:.1f}" y="{b:.1f}" width="{c - a:.1f}" height="{d - b:.1f}" rx="5" fill="{fill}" stroke="{stroke}" stroke-width="2" stroke-dasharray="7 5"/>')
        if label:
            add(f'<text x="{(a + 6) if lx is None else X(lx):.1f}" y="{(b + 16) if ly is None else Y(ly):.1f}" font-size="{size}" font-weight="{weight}" text-anchor="{anchor}" fill="{stroke}">{escape(label)}</text>')

    hang(1, 3, 60, 40, None, "#2b5fb3", "none")
    hang(62, 6, 50, 34, None, "#5a6372", "none")
    hang(64, 6.5, 46, 33, None, "#9a5b25", "none")
    hang(67.75, 13.5, 38.5, 19, None, "#a0272d", "#a0272d14")
    for tx, ty, t, col in [(1.2, 17.35, "RC522", "#2b5fb3"), (44.8, 16.0, "BAT", "#5a6372"), (45.0, 3.4, "COIL", "#9a5b25"), (41.2, 13.0, "RDM", "#a0272d")]:
        add(f'<text x="{X(tx):.1f}" y="{Y(ty):.1f}" font-size="11" font-weight="700" fill="{col}">{t}</text>')
    for fx, fy, tall in [(1.2, 3.2, 1), (1.2, 38.8, 1), (56.2, 3.2, 1), (56.2, 38.8, 1),
                         (73, 7, 0), (73, 35, 0), (108, 7, 0), (108, 35, 0)]:
        hang(fx, fy, 4, 4, None, "#8a6a2a", "#c9a46b88" if tall else "#c9a46bcc")

    # module bodies on top of the plate
    fills = {"ESP32-C6": "#2d7a4d", "Pico-LCD": "#1c2230", "TP4056": "#1746b8", "MT3608": "#0b1f6b"}
    for m in mp.MODULES:
        col = next((v for k, v in fills.items() if m["name"].startswith(k)), "#444")
        add(f'<rect x="{X(m["x0"]):.1f}" y="{Y(m["y0"]):.1f}" width="{(m["x1"] - m["x0"]) * S:.1f}" height="{(m["y1"] - m["y0"]) * S:.1f}" '
            f'rx="5" fill="{col}" fill-opacity="0.07" stroke="{col}" stroke-width="2"/>')
    for z in mp.ZONES:
        add(f'<rect x="{X(z["x0"]):.1f}" y="{Y(z["y0"]):.1f}" width="{(z["x1"] - z["x0"]) * S:.1f}" height="{(z["y1"] - z["y0"]) * S:.1f}" '
            f'fill="#ff4d4d" fill-opacity="0.10" stroke="#d33" stroke-width="1.4" stroke-dasharray="4 4"/>')
        add(f'<text x="{X(z["x0"]) + 3:.1f}" y="{Y(z["y1"]) - 6:.1f}" font-size="11" fill="#b02020">antenna: no wire/part</text>')

    c6 = mp.MODULES[0]
    add(f'<text x="{X(c6["x0"]) + 8:.1f}" y="{Y(1.15):.1f}" font-size="13" font-weight="700" fill="#1f5c39">ESP32-C6 DevKit (USB-C ←  → antenna): 2 × 16 female header, rows {mp.ROW_GAP} holes apart</text>')
    lcd = mp.MODULES[1]
    add(f'<text x="{X(lcd["x0"]) + 8:.1f}" y="{Y(9.6):.1f}" font-size="13" font-weight="700" fill="#242c3e">Pico-LCD-1.3 (sits above the plate)</text>')
    add(f'<text x="{X(lcd["x0"]) + 8:.1f}" y="{Y(10.4):.1f}" font-size="12" fill="#4a5568">2 × 20 male pins, pin 1 on the left, rows 7 holes apart</text>')
    for m, t in zip(mp.MODULES[2:], ("TP4056 charger (on 1 mm foam)", "MT3608 boost (on 1 mm foam)")):
        add(f'<text x="{X(m["x0"]) + 8:.1f}" y="{Y(m["y0"]) + 20:.1f}" font-size="13" font-weight="700" fill="{fills["TP4056"] if "TP" in m["name"] else fills["MT3608"]}">{t}</text>')
        add(f'<text x="{X(m["x0"]) + 8:.1f}" y="{Y(m["y0"]) + 38:.1f}" font-size="12" fill="#4a5568">flying leads, no holes under it</text>')

    # off-plate parts that sit on top of the plate (mm positions from bare_stack.html)
    def top(x0, y0, dx, dy, label, stroke="#333"):
        a, b, c, d = X(mm2h(x0)), Y(mm2h(y0)), X(mm2h(x0 + dx)), Y(mm2h(y0 + dy))
        add(f'<rect x="{a:.1f}" y="{b:.1f}" width="{c - a:.1f}" height="{d - b:.1f}" rx="6" fill="#ffffffaa" stroke="{stroke}" stroke-width="1.6"/>')
        add(f'<text x="{(a + c) / 2:.1f}" y="{(b + d) / 2 + 4:.1f}" font-size="11" font-weight="600" text-anchor="middle" fill="{stroke}">{escape(label)}</text>')

    top(57.5, 18, 8.5, 9.4, "switch")
    top(102, 3, 14, 6, "vibration motor")
    top(93, 1, 7, 6, "VS1838B")

    # wires (underside, through the board)
    if with_wires:
        for w in mp_wires:
            add(f'<line x1="{X(w["ax"]):.1f}" y1="{Y(w["ay"]):.1f}" x2="{X(w["bx"]):.1f}" y2="{Y(w["by"]):.1f}" '
                f'stroke="{w["color"]}" stroke-width="2.4" stroke-opacity="0.8" stroke-linecap="round"/>')
            mx, my = (X(w["ax"]) + X(w["bx"])) / 2, (Y(w["ay"]) + Y(w["by"])) / 2
            if not w["short"]:
                add(f'<text x="{mx:.1f}" y="{my - 3:.1f}" font-size="9" fill="#1b2430" stroke="#fbfbf8" stroke-width="2.5" paint-order="stroke">{w["n"]}</text>')

    # component bodies (resistors, diodes, caps, transistors) behind the pads
    by_ref = {}
    for p in mp.pads:
        by_ref.setdefault(p["ref"], []).append(p)
    kinds = {pt["ref"]: pt for pt in mp.parts}
    for ref, ps in by_ref.items():
        pt = kinds.get(ref)
        if not pt or len(ps) < 2:
            continue
        xs, ys = [p["x"] for p in ps], [p["y"] for p in ps]
        a, b, c, d = X(min(xs)), Y(min(ys)), X(max(xs)), Y(max(ys))
        if pt["kind"] in ("res", "diode"):
            horiz = abs(xs[0] - xs[1]) > 0
            t = 0.36 * S
            fill = "#e8d9b0" if pt["kind"] == "res" else "#c8c8d4"
            if horiz:
                add(f'<rect x="{a:.1f}" y="{b - t / 2:.1f}" width="{c - a:.1f}" height="{t:.1f}" rx="4" fill="{fill}" stroke="#7a6a3a" stroke-width="1.2"/>')
            else:
                add(f'<rect x="{a - t / 2:.1f}" y="{b:.1f}" width="{t:.1f}" height="{d - b:.1f}" rx="4" fill="{fill}" stroke="#7a6a3a" stroke-width="1.2"/>')
            lbl = pt["value"] or ("1N5819" if pt["kind"] == "diode" else "")
            if pt["kind"] == "diode":
                k = next(p for p in ps if p["pin"] == "K")
                add(f'<rect x="{X(k["x"]) - 9:.1f}" y="{Y(k["y"]) - t / 2:.1f}" width="4" height="{t:.1f}" fill="#222"/>')
            if lbl:
                cx, cy = (a + c) / 2, (b + d) / 2
                if horiz:
                    add(f'<text x="{cx:.1f}" y="{cy + 3.5:.1f}" font-size="10" font-weight="700" text-anchor="middle" fill="#3a3320">{escape(lbl)}</text>')
                else:
                    add(f'<text x="{cx + t / 2 + 4:.1f}" y="{cy + 3.5:.1f}" font-size="10" font-weight="700" fill="#3a3320">{escape(lbl)}</text>')
        elif pt["kind"] == "cap":
            cx, cy = (a + c) / 2, (b + d) / 2
            horiz = abs(xs[0] - xs[1]) > 0
            rx, ry = (abs(c - a) / 2 + 12, 14) if horiz else (14, abs(d - b) / 2 + 12)
            add(f'<ellipse cx="{cx:.1f}" cy="{cy:.1f}" rx="{rx:.1f}" ry="{ry:.1f}" fill="#2b2f3a" fill-opacity="0.18" stroke="#2b2f3a" stroke-width="1.4"/>')
            if pt["value"]:
                add(f'<text x="{cx:.1f}" y="{cy + 4:.1f}" font-size="10" font-weight="700" text-anchor="middle" fill="#1b2430">{escape(pt["value"])}</text>')
        elif pt["kind"] == "tr":
            add(f'<rect x="{a - 15:.1f}" y="{b - 15:.1f}" width="30" height="{d - b + 30:.1f}" rx="14" fill="#d9d9e2" stroke="#444" stroke-width="1.4"/>')
            add(f'<text x="{a - 22:.1f}" y="{b + 4:.1f}" font-size="12" font-weight="700" text-anchor="end" fill="#1b2430">{escape(ref)}</text>')
        elif pt["kind"] == "header" and ref not in ("J1", "J3", "LCD"):
            add(f'<rect x="{a - 15:.1f}" y="{b - 15:.1f}" width="{c - a + 30:.1f}" height="{d - b + 30:.1f}" rx="12" fill="#ffffff66" stroke="#555" stroke-width="1.2"/>')
    # header outlines for the C6 strips and the LCD
    for ref in ("J1", "J3"):
        ps = by_ref[ref]
        a, c = X(min(p["x"] for p in ps)), X(max(p["x"] for p in ps))
        yy = Y(ps[0]["y"])
        add(f'<rect x="{a - 15:.1f}" y="{yy - 15:.1f}" width="{c - a + 30:.1f}" height="30" rx="12" fill="#ffffff55" stroke="#2d7a4d" stroke-width="1.4"/>')
    for yy in (mp.LROW_LOW, mp.LROW_UP):
        a, c = X(27), X(46)
        add(f'<rect x="{a - 15:.1f}" y="{Y(yy) - 15:.1f}" width="{c - a + 30:.1f}" height="30" rx="12" fill="#ffffff44" stroke="#242c3e" stroke-width="1.4"/>')

    # pads + short labels
    for p in mp.pads:
        cx, cy = X(p["x"]), Y(p["y"])
        used = bool(p["net"])
        r = 0.42 * S if (used or p["forbid"]) else 3
        fill = netcol(p["net"]) if used else "#9fb3a6"
        add(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}" fill="{fill}" stroke="#1b2430" stroke-width="{1.3 if used else 0.6}"/>')
        if p["forbid"]:
            add(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{0.46 * S:.1f}" fill="none" stroke="#e0242b" stroke-width="2.4"/>')
            add(f'<path d="M{cx - 8:.1f},{cy - 8:.1f} L{cx + 8:.1f},{cy + 8:.1f} M{cx + 8:.1f},{cy - 8:.1f} L{cx - 8:.1f},{cy + 8:.1f}" stroke="#e0242b" stroke-width="2.4"/>')
        elif p["strap"] and p["ref"] in ("J1", "J3"):
            add(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{0.5 * S:.1f}" fill="none" stroke="#e08a00" stroke-width="1.6" stroke-dasharray="3 3"/>')
        lab = short(p["label"]) if (used or p["forbid"]) and p["label"] else ""
        if lab and p["ref"] not in ("R1", "R2", "R7", "R8", "R9", "Rled", "C3") and not p["ref"].startswith(("Rb", "Rp")):
            fs = 8.2 if len(lab) >= 4 else (9.5 if len(lab) >= 3 else 11)
            add(f'<text x="{cx:.1f}" y="{cy + fs * 0.36:.1f}" font-size="{fs}" font-weight="700" text-anchor="middle" fill="#0b0f16" '
                f'stroke="#ffffff99" stroke-width="2" paint-order="stroke">{escape(lab)}</text>')

    # captions for headers
    cap = lambda hx, hy, txt, anchor="start", size=12: add(
        f'<text x="{X(hx):.1f}" y="{Y(hy):.1f}" font-size="{size}" font-weight="600" text-anchor="{anchor}" fill="#1b2430" stroke="#fbfbf8" stroke-width="3" paint-order="stroke">{escape(txt)}</text>')

    # legend
    ly = MT + H * S + 36
    add(f'<text x="{MX}" y="{ly}" font-size="16" font-weight="700" fill="#1b2430">Nets (pad fill colour)</text>')
    nets = ["3V3", "5V", "GND", "VBAT", "VLOAD", "SCK", "MOSI", "MISO", "LCD_CS", "LCD_DC", "LCD_RST", "LCD_BL",
            "BTN_UP", "BTN_DOWN", "BTN_LEFT", "BTN_RIGHT", "BTN_A", "RC_CS", "RC_RST", "RDM_TX", "IR_RX", "IR_TX", "FB", "BAT_SENSE"]
    for i, n in enumerate(nets):
        cx = MX + (i % 6) * 245
        cy = ly + 26 + (i // 6) * 26
        add(f'<circle cx="{cx + 8}" cy="{cy - 4}" r="8" fill="{netcol(n)}" stroke="#1b2430" stroke-width="1"/>')
        add(f'<text x="{cx + 24}" y="{cy}" font-size="13" fill="#1b2430">{escape(n)}</text>')
    iy = ly + 26 + 4 * 26 + 18
    sw = [("#2b5fb3", "RC522 60×40 (antenna down)"), ("#5a6372", "battery 50×34, cardboard above/below"),
          ("#9a5b25", "125 kHz coil 46×33, read face down"), ("#a0272d", "RDM6300 board, inside the coil"),
          ("#8a6a2a", "bridge feet (4 tall under RC522, 4 short under battery)")]
    for i, (col, t) in enumerate(sw):
        cx = MX + (i % 3) * 400
        cy = iy + (i // 3) * 22
        add(f'<rect x="{cx}" y="{cy - 12}" width="26" height="14" rx="3" fill="none" stroke="{col}" stroke-width="2" stroke-dasharray="5 3"/>')
        add(f'<text x="{cx + 34}" y="{cy}" font-size="13" fill="#1b2430">{escape("dashed: " + t)}</text>')
    iy += 56
    notes = [
        "Top strip holes (y 1–3): RDM6300 wires x27–29 (TX, GND, 5V) + 1k/1k/1k divider; RC522 wires x27–34 (CS SCK MOSI MISO IRQ GND RST 3V3).",
        "Red ring with X = never wire (IO12/IO13, TX/RX: USB / console).   Orange dashed ring = strapping pin: do not pull low at reset.",
        "Resistors/diodes are drawn between their two holes; the body covers the holes in between, so nothing else goes there.",
        "Positions of the dashed items come from bare_stack.html (mm); hole positions of everything else come from make_plan_bare.py.",
    ]
    for i, t in enumerate(notes):
        add(f'<text x="{MX}" y="{iy + i * 20}" font-size="13" fill="#4a5568">{escape(t)}</text>')
    add("</svg>")
    return "\n".join(o)


mp_wires = mp.build_wires()
for name, wires in (("bare_board_top.svg", False), ("bare_board_top_wires.svg", True)):
    with open(os.path.join(HERE, name), "w", encoding="utf-8", newline="\n") as f:
        f.write(build(wires))
print("wrote bare_board_top.svg and bare_board_top_wires.svg", SVG_W, "x", SVG_H)
