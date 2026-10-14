#!/usr/bin/env python3
"""Perfboard wiring plan generator (single source of truth: hole coordinates).

Run:  python make_plan.py
Out:  perfboard_plan.html   (interactive step-by-step guide)
      ../PERFBOARD_BAGLANTI_PLANI.md  (same wire list as text)

Grid: 38 x 38 holes (10x10 cm board, 2.54 mm pitch). x grows right, y grows
down, viewed from the COMPONENT side. USB-C end of the ESP32 faces down.
Everything geometric lives in this file: change one number, re-run.

Sources: ESP32-C6-DevKitC-1 header tables (Espressif; Waveshare says the
-NX/-N8 board is pin-compatible), Waveshare Pico-LCD-1.3 wiki (signals ->
Pico GP pins; Pico 2x20 numbering is the standard Raspberry Pi Pico one),
and this repo's pin map (main/*.c, WIRING_MAP_RC522.md).
"""
import itertools
import json
import math
import os
from collections import defaultdict

W, H = 38, 38
HERE = os.path.dirname(os.path.abspath(__file__))

pads, parts = [], []


def pad(ref, pin, x, y, net, stage, label=None, lab=None, forbid=False, strap=False):
    pads.append(dict(id=f"{ref}.{pin}", ref=ref, pin=str(pin), x=x, y=y, net=net,
                     stage=stage, label=label if label is not None else str(pin),
                     lab=lab, forbid=forbid, strap=strap))


def part(ref, name, kind, stage, note="", value=""):
    parts.append(dict(ref=ref, name=name, kind=kind, stage=stage, note=note, value=value))


# --------------------------------------------------------------------------
# ESP32-C6 DevKit: two 1x16 female strips, 9 holes apart (22.86 mm: Espressif DevKitC-1
# layout drawing; Waveshare's own outline is not published as text, so measure yours)
# --------------------------------------------------------------------------
J1X, J3X, CY = 11, 20, 3
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
part("J1", "ESP32-C6 sol şerit (J1)", "header", 1, "1x16 dişi header")
part("J3", "ESP32-C6 sağ şerit (J3)", "header", 1, "1x16 dişi header")
for i, n in enumerate(J1):
    pad("J1", i + 1, J1X, CY + i, J1NET.get(n), 1, n, lab="r", strap=n in STRAP)
for i, n in enumerate(J3):
    pad("J3", i + 1, J3X, CY + i, J3NET.get(n), 1, n, lab="l",
        forbid=n in FORBID, strap=n in STRAP)

# --------------------------------------------------------------------------
# Pico-LCD-1.3: its female 2x40 socket mates with MALE pins in Pico layout.
# --------------------------------------------------------------------------
LLX, LRX, LY = 25, 32, 3
PICO = {1: "GP0", 2: "GP1", 3: "GND", 4: "GP2", 5: "GP3", 6: "GP4", 7: "GP5", 8: "GND",
        9: "GP6", 10: "GP7", 11: "GP8", 12: "GP9", 13: "GND", 14: "GP10", 15: "GP11",
        16: "GP12", 17: "GP13", 18: "GND", 19: "GP14", 20: "GP15", 21: "GP16",
        22: "GP17", 23: "GND", 24: "GP18", 25: "GP19", 26: "GP20", 27: "GP21",
        28: "GND", 29: "GP22", 30: "RUN", 31: "GP26", 32: "GP27", 33: "GND", 34: "GP28",
        35: "VREF", 36: "3V3", 37: "3V3EN", 38: "GND", 39: "VSYS", 40: "VBUS"}
# pico pin -> (net, stage, short label)
LCDUSE = {4: ("BTN_UP", 3, "UP"), 11: ("LCD_DC", 2, "DC"), 12: ("LCD_CS", 2, "CS"),
          14: ("SCK", 2, "CLK"), 15: ("MOSI", 2, "DIN"), 16: ("LCD_RST", 2, "RST"),
          17: ("LCD_BL", 2, "BL"), 20: ("BTN_A", 3, "A"), 21: ("BTN_LEFT", 3, "LEFT"),
          24: ("BTN_DOWN", 3, "DOWN"), 26: ("BTN_RIGHT", 3, "RIGHT"),
          39: ("3V3", 1, "VCC"), 38: ("GND", 1, "GND")}
part("LCD", "Pico-LCD-1.3 (erkek pin 2x20, Pico dizilimi)", "header", 1,
     "ekranın DİŞİ soketi (hazır lehimli) bu pinlere oturur; yön: ekrandaki USB yazılı uç = pin 1 ucu")
for p in range(1, 41):
    if p <= 20:
        x, y, lab = LLX, LY + p - 1, "r"
    else:
        x, y, lab = LRX, LY + (40 - p), "l"
    net, st, sh = LCDUSE.get(p, (None, 1, None))
    pad("LCD", p, x, y, net, st, f"{sh} · {PICO[p]}" if sh else PICO[p],
        lab=lab if sh else None)

# --------------------------------------------------------------------------
# Left zone: ports (Dupont F-F cables go from these to the loose modules)
# --------------------------------------------------------------------------
part("J_RC522", "RC522 portu (1x8 erkek)", "header", 5,
     "RC522 sırası: SDA SCK MOSI MISO IRQ GND RST 3.3V — modülün yazısıyla doğrula")
for i, (n, net) in enumerate([("SDA/CS", "RC_CS"), ("SCK", "SCK"), ("MOSI", "MOSI"),
                              ("MISO", "MISO"), ("IRQ", None), ("GND", "GND"),
                              ("RST", "RC_RST"), ("3.3V", "3V3")]):
    pad("J_RC522", i + 1, 2, 5 + i, net, 5, n, lab="r")

part("IR_RX", "VS1838B IR alıcı (1x3 dişi)", "header", 7,
     "çıplak VS1838B (kubbe sana bakarken soldan): OUT, GND, VCC. KY-022 kartında sıra GND, VCC, S: yazıyı oku")
for i, (n, net) in enumerate([("OUT", "IR_RX"), ("GND", "GND"), ("VCC", "3V3")]):
    pad("IR_RX", i + 1, 5 + i, 2, net, 7, n, lab="b")

part("J_RDM", "RDM6300 portu (1x3 erkek)", "header", 6, "RDM6300 P1 sırası: TX, RX, NC, GND, VCC(5V). Üç ayrı Dupont: TX, GND, VCC. RX boş kalır")
for i, (n, net) in enumerate([("TX", "RDM_TXIN"), ("GND", "GND"), ("5V", "5V")]):
    pad("J_RDM", i + 1, 2, 15 + i, net, 6, n, lab="r")
for ref, x, top, bot, val in [("R7", 4, "RDM_TXIN", "RDM_TX", "1k"),
                              ("R8", 6, "RDM_MID", "RDM_TX", "1k"),
                              ("R9", 8, "RDM_MID", "GND", "1k")]:
    part(ref, f"{ref} {val} (bölücü)", "res", 6, "5V→3.3V bölücü: 1k + (1k+1k)", val)
    pad(ref, 1, x, 15, top, 6, "", lab=None)
    pad(ref, 2, x, 19, bot, 6, "", lab=None)

part("J_5V", "5V giriş (1x2 erkek)", "header", 8, "MT3608 çıkışı → 1N5819 → buraya")
pad("J_5V", 1, 2, 21, "5V", 8, "5V", lab="r")
pad("J_5V", 2, 2, 22, "GND", 8, "GND", lab="r")
part("C2", "C2 100µF (RDM6300 5V'u)", "cap", 8, "uzun bacak + (5V)", "100µF")
pad("C2", 1, 5, 21, "5V", 8, "+", lab=None)
pad("C2", 2, 7, 21, "GND", 8, "−", lab=None)

part("J_VBAT", "Pil hattı (1x2 erkek)", "header", 8, "anahtardan SONRAKİ pil hattı (3.0–4.2V)")
pad("J_VBAT", 1, 17, 36, "VBAT", 8, "VBAT", lab="t")
pad("J_VBAT", 2, 18, 36, "GND", 8, "GND", lab="b")
part("R1", "R1 10k (pil bölücü üst)", "res", 8, "", "10k")
pad("R1", 1, 22, 31, "VBAT", 8, "")
pad("R1", 2, 22, 35, "BAT_SENSE", 8, "")
part("R2", "R2 10k (pil bölücü alt)", "res", 8, "", "10k")
pad("R2", 1, 24, 31, "GND", 8, "")
pad("R2", 2, 24, 35, "BAT_SENSE", 8, "")
part("C3", "C3 100nF (ADC filtre, ops.)", "cap", 8, "", "100nF")
pad("C3", 1, 26, 34, "BAT_SENSE", 8, "")
pad("C3", 2, 26, 36, "GND", 8, "")

# --------------------------------------------------------------------------
# Bottom zone: IR LED driver, vibration motor driver, buzzer, load selector.
# Generic NPN low-side switch template (E B C in a row, pull-down on the left).
# --------------------------------------------------------------------------
def nswitch(tag, ox, oy, stage, rb_val, in_net, load_k, load_a, qlabel):
    q = f"Q{tag}"
    part(f"Rb{tag}", f"Rb{tag} {rb_val} (baz)", "res", stage, "", rb_val)
    pad(f"Rb{tag}", 1, ox + 1, oy, in_net, stage, "")
    pad(f"Rb{tag}", 2, ox + 1, oy + 4, f"{q}_B", stage, "")
    part(f"Rp{tag}", f"Rp{tag} 10k (baz→GND)", "res", stage, "", "10k")
    pad(f"Rp{tag}", 1, ox - 1, oy + 1, f"{q}_B", stage, "")
    pad(f"Rp{tag}", 2, ox - 1, oy + 5, "GND", stage, "")
    part(q, f"{q} 2N2222 ({qlabel})", "tr", stage,
         "TO-92 pin sırası üreticiye göre değişir (PN2222A: E-B-C, P2N2222A: C-B-E). Lehimden önce multimetre hFE soketiyle doğrula.")
    pad(q, "E", ox, oy + 5, "GND", stage, "E", lab="b")
    pad(q, "B", ox + 1, oy + 5, f"{q}_B", stage, "B", lab="b")
    pad(q, "C", ox + 2, oy + 5, f"{q}_C", stage, "C", lab="b")
    return q


# IR transmitter
nswitch("1", 3, 29, 7, "1k", "IR_TX", None, None, "IR LED sürücü")
part("LED1", "IR LED 940nm (1x2 dişi soket)", "header", 7,
     "Çıplak LED: kısa bacak = katot (K). KY-005 modülünde LED'in iki ucunu multimetre diyot modunda bul (S ve − pini).")
pad("LED1", "K", 7, 34, "Q1_C", 7, "K", lab="b")
pad("LED1", "A", 8, 34, "LED_A", 7, "A", lab="b")
part("Rled", "Rled 33Ω (LED seri)", "res", 7, "33Ω = 3 adet 100Ω paralel (siparişteki 100R paketinden). KY-005 üzerinde direnç varsa bu direnci küçült.", "33Ω")
pad("Rled", 1, 9, 29, "VLOAD", 7, "")
pad("Rled", 2, 9, 33, "LED_A", 7, "")

# Vibration motor
nswitch("2", 14, 29, 4, "470Ω", "FB", None, None, "motor sürücü")
part("M1", "Titreşim motoru (1x2 dişi soket)", "header", 4, "motor kutupsuz; tel uçlarını dişi sokete")
pad("M1", "−", 18, 34, "Q2_C", 4, "M−", lab="b")
pad("M1", "+", 19, 34, "VLOAD", 4, "M+", lab="b")
part("D1", "D1 1N5819 (ters/flyback)", "diode", 4, "BANTLI uç = katot → VLOAD (M+)")
pad("D1", "K", 17, 30, "VLOAD", 4, "")
pad("D1", "A", 17, 34, "Q2_C", 4, "")

# Active buzzer straight on IO1 (shares the net with the motor driver input)
part("BZ1", "Aktif buzzer (1x2 dişi soket)", "header", 4, "+ bacağı (uzun) → IO1, − → GND")
pad("BZ1", "+", 24, 29, "FB", 4, "BZ+", lab="b")
pad("BZ1", "−", 25, 29, "GND", 4, "BZ−", lab="b")

# Load selector jumper: 3V3 (USB test) <-> VBAT (battery). Cap goes on 1-2 or 2-3.
part("JP1", "Yük seçici JP1 (1x3 erkek + jumper)", "header", 4,
     "jumper 1-2 = USB testi (3V3), 2-3 = pil (VBAT)")
pad("JP1", 1, 12, 36, "3V3", 4, "3V3", lab="b")
pad("JP1", 2, 13, 36, "VLOAD", 4, "VLOAD", lab="t")
pad("JP1", 3, 14, 36, "VBAT", 4, "VBAT", lab="b")

part("C1", "C1 1000µF (5V hattı)", "cap", 8, "uzun bacak + (5V). Pil aşamasında", "1000µF")
pad("C1", 1, 5, 25, "5V", 8, "+", lab=None)
pad("C1", 2, 7, 25, "GND", 8, "−", lab=None)

# --------------------------------------------------------------------------
# Module bodies for the diagram (hole units)
# --------------------------------------------------------------------------
MODULES = [
    dict(name="ESP32-C6 DevKit (üstten)", x0=10.5, y0=0.3, x1=20.5, y1=20.7,
         note="anten ↑ · USB-C ↓"),
    dict(name="Pico-LCD-1.3", x0=23.3, y0=2.27, x1=33.7, y1=22.73, note="pin-1 üstte"),
]
ZONES = [
    dict(name="ANTEN BÖLGESİ: tel/parça yok", x0=10.5, y0=0.3, x1=20.5, y1=3.6, kind="keepout"),
    dict(name="USB KABLO KORİDORU: alçak parça", x0=11, y0=20.8, x1=20, y1=29, kind="corridor"),
]

STAGES = [
    dict(n=1, title="İskelet: soketler + 3V3/GND",
         intro=["C6'yı iki dişi 1×16 şeride, ekranı 2×(1×20) ERKEK şeride tak; şeritleri modüle takılıyken plakaya dizip öyle lehimle (aralık kendiliğinden doğru olur).",
                "Sadece güç: C6 3V3 → ekran VCC (VSYS pini), GND hattı.",
                "Ekranı pin-1 ucu ÜSTTE olacak şekilde tak."],
         test="Güç VERMEDEN multimetre süreklilik: 3V3 ↔ GND bipleme­meli. Sonra sadece C6'yı USB ile tak: açılıyor mu?"),
    dict(n=2, title="Ekran sinyalleri (6 tel)",
         intro=["SPI + kontrol hatları: CLK, DIN, CS, DC, RST, BL.",
                "Teller birbirini çaprazlar: bu normal (iki konnektör karşılıklı duruyor)."],
         test="Mevcut firmware'i yükle: menü ekranda görünmeli."),
    dict(n=3, title="Joystick + A (5 tel)",
         intro=["UP, DOWN, LEFT, RIGHT, A. Hepsi ekran modülünün üstünde, ortak GND'si zaten bağlı."],
         test="Menüde UP/DOWN/RIGHT/LEFT/A ile gez."),
    dict(n=4, title="Buzzer + titreşim motoru + yük seçici",
         intro=["JP1'e jumper'ı 1-2'ye tak (USB testi, 3V3).",
                "IO1 hem buzzer'a hem motor sürücüsünün baz direncine gider.",
                "D1'in bantlı ucu (katot) M+ tarafına bakar.",
                "Siparişte 470Ω ve 10k yok: Rb2 için 2×1k paralel (=500Ω) kullan. Rp (baz→GND) zorunlu değil, boş bırakılabilir ya da 10k al."],
         test="Bir menüye girince buzzer ötmeli, titreşim sezilmeli."),
    dict(n=5, title="RC522 portu (6 tel)",
         intro=["SCK ve MOSI ekranla ORTAK: aynı C6 deliğine ikinci bir tel gelir.",
                "Port sırası RC522 modülünün sırasıyla aynı olsun ki 8'li Dupont kablo düz takılsın."],
         test="hardware_profile.h → BOARD_HAS_RC522 = 1, flashla. RFID menüsünde kart okut: UID."),
    dict(n=6, title="RDM6300 portu + bölücü",
         intro=["TX (5V seviyesi) → 1k → [IO15 düğümü] → 1k+1k → GND: 5V → 3.3V.",
                "RDM6300 5V ile beslenir (C6'nın 5V pini).",
                "Önce RDM6300'ü 5V'a verip TX'i multimetreyle ölç. ~5V ise bölücü şart; ~3.3V ise bölücüyü ATLA (3.3V → 2.2V'a düşer, C6 girişi HIGH sayamayabilir) ve TX'i doğrudan IO15'e ver."],
         test="125kHz anahtarlığı bobine yaklaştır: ID görünmeli."),
    dict(n=7, title="IR alıcı + IR verici",
         intro=["VS1838B: OUT → IO4.",
                "Verici: IO0 → 1k → 2N2222 baz; LED, 33Ω üzerinden VLOAD'a.",
                "Elindeki verici KY-005 MODÜLÜ (çıplak LED değil). Kaynaklar orta pini çelişkili anlatıyor: lehimden önce LED'in iki ucunu multimetre diyot modunda bul, orta pini bağlama.",
                "Q1/Q2'yi lehimlemeden önce pin sırasını hFE soketiyle doğrula (PN2222A E-B-C, P2N2222A C-B-E)."],
         test="IR Öğren ile kumanda kodu yakala; IR Gönderim Testi'ni telefon kamerasıyla izle (mor yanıp söner)."),
    dict(n=8, title="Pil ölçer + güç girişleri",
         intro=["Pil bölücü (R1,R2) ve J_VBAT, J_5V portları, kondansatörler.",
                "En son: MT3608'i yüksüz 5.3V'a ayarla, sonra bağla (docs/planning/ERDEM_SATIN_ALINACAKLAR.md)."],
         test="Ekranda pil yüzdesi çıkmalı. JP1 jumper'ını 2-3'e al."),

    dict(n=9, title="Güç zinciri (modüller arası, plaka dışı)",
         intro=["Bu adım plakada delik değil, modüller arası kablolar. Sıra önemli: önce MT3608'i ayarla, sonra bağla.",
                "MT3608'i hiçbir şeye bağlamadan, pile bağlayıp çıkışı multimetreyle ölç ve 5.3V'a getir. Trimmer yönü satıcıya göre ters olabilir: küçük adımlarla çevir, ölçerek yönü bul.",
                "TP4056 korumalı olmalı (B+/B− ve ayrıca OUT+/OUT− pedleri var). Yük OUT pedlerinden alınır, pil B pedlerine bağlanır.",
                "Siparişteki TP4056 modülü 9.8 TL: bu fiyat korumasız (sadece B+/B−) modül olduğunu düşündürüyor ama doğrulanmadı. Kartında OUT+/OUT− pedi yoksa pilin kendi koruma devresi (PCM) olmadan kullanma; korumalı TP4056 al.",
                "Şarj ederken cihazı kapat; karta USB takmadan önce anahtarı kapat."],
         test="Anahtar açık, USB yok: ekran açılmalı, pil yüzdesi görünmeli. JP1 jumper'ı 2-3'e alınır."),
]
HARNESS = [
    ("LiPo (+)", "TP4056 B+", "kırmızı", "JST kablosu"),
    ("LiPo (−)", "TP4056 B−", "siyah", ""),
    ("TP4056 OUT+", "PPTC 1.1A → anahtar giriş", "kırmızı", "sigorta ile anahtar arasına"),
    ("anahtar çıkış", "J_VBAT.VBAT (plaka) + MT3608 VIN+", "kırmızı", "bu hat = anahtardan sonraki pil hattı (3.0–4.2V)"),
    ("TP4056 OUT−", "J_VBAT.GND (plaka) + MT3608 VIN−", "siyah", ""),
    ("MT3608 VOUT+", "1N5819 anot", "turuncu", "diyot USB'ye geri beslemeyi önler"),
    ("1N5819 katot (bantlı uç)", "J_5V.5V (plaka)", "turuncu", "1000µF C1 zaten bu hatta"),
    ("MT3608 VOUT−", "J_5V.GND (plaka)", "siyah", ""),
    ("JP1 jumper", "pin 2-3 (VLOAD = VBAT)", "—", "USB testinde 1-2 idi"),
]

# --------------------------------------------------------------------------
# Wires: per net, per stage, Kruskal with C6 same-net pads pre-joined
# --------------------------------------------------------------------------
NETCOL = {  # net -> (hex, Turkish colour suggestion)
    "3V3": ("#ff4d4d", "kırmızı"), "5V": ("#ff9a1f", "turuncu"), "GND": ("#cfd8dc", "siyah"),
    "VBAT": ("#ff5fa2", "pembe"), "VLOAD": ("#ff8fb8", "pembe"),
    "SCK": ("#ffd21a", "sarı"), "MOSI": ("#ff9f1c", "turuncu"), "MISO": ("#1fd6e6", "açık mavi"),
    "LCD_CS": ("#27d651", "yeşil"), "LCD_DC": ("#3d7bff", "mavi"), "LCD_RST": ("#a24dff", "mor"),
    "LCD_BL": ("#f0f3f8", "beyaz"), "BTN_UP": ("#5ec8ff", "açık mavi"), "BTN_DOWN": ("#5e8cff", "mavi"),
    "BTN_LEFT": ("#9ad66a", "açık yeşil"), "BTN_RIGHT": ("#ff7b7b", "kırmızı"),
    "BTN_A": ("#ffb36b", "turuncu"), "RC_CS": ("#26a69a", "yeşil"), "RC_RST": ("#ef5350", "kırmızı"),
    "RDM_TX": ("#ff6f00", "turuncu"), "IR_RX": ("#42a5f5", "mavi"), "IR_TX": ("#ab47bc", "mor"),
    "FB": ("#66bb6a", "yeşil"), "BAT_SENSE": ("#ffee58", "sarı"),
}
DEFCOL = ("#b0bec5", "gri")


def dist(a, b):
    return math.hypot(a["x"] - b["x"], a["y"] - b["y"])


def route(a, b):
    """Polyline a -> (side exit) -> b. A wire that leaves a C6 strip pad and goes
    far up/down first steps sideways: into the empty corridor under the module
    (between the strips) or out to the left/right, never along the strip."""
    pts = [(a["x"], a["y"]), (b["x"], b["y"])]
    for end, other in ((a, b), (b, a)):
        if end["ref"] in ("J1", "J3") and abs(other["y"] - end["y"]) > 3:
            if end["ref"] == "J1":
                vx = 9 if other["x"] < J1X else J1X + 2
            else:
                vx = J3X + 2 if other["x"] > J3X else J3X - 2
            via = (vx, end["y"])
            pts = [(a["x"], a["y"]), via, (b["x"], b["y"])]
            break
    length = sum(math.hypot(p[0] - q[0], p[1] - q[1]) for p, q in zip(pts, pts[1:]))
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
    c6 = MODULES[0]
    for p in pads:
        if p["ref"] in ("J1", "J3"):
            continue
        inside = c6["x0"] < p["x"] < c6["x1"] and c6["y0"] < p["y"] < c6["y1"]
        assert not inside, f"{p['id']} inside the C6 body"
    lcd = MODULES[1]
    for p in pads:
        if p["ref"] == "LCD":
            continue
        inside = lcd["x0"] < p["x"] < lcd["x1"] and lcd["y0"] < p["y"] < lcd["y1"]
        assert not inside, f"{p['id']} inside the LCD body"


def main():
    checks()
    wires = build_wires()
    data = dict(W=W, H=H, pads=pads, parts=parts, wires=wires, stages=STAGES, harness=HARNESS,
                modules=MODULES, zones=ZONES, netcol={k: v[0] for k, v in NETCOL.items()})
    with open(os.path.join(HERE, "template.html"), encoding="utf-8") as f:
        tpl = f.read()
    html = tpl.replace("/*__DATA__*/null", json.dumps(data, ensure_ascii=False))
    with open(os.path.join(HERE, "perfboard_plan.html"), "w", encoding="utf-8") as f:
        f.write(html)
    write_markdown(wires)
    per = defaultdict(int)
    for w in wires:
        per[w["stage"]] += 1
    print("wires per stage:", dict(per), "total", len(wires))
    print("longest:", max(wires, key=lambda w: w["holes"])["a"], "->",
          max(wires, key=lambda w: w["holes"])["b"], max(w["holes"] for w in wires), "holes")


def pad_label(pid):
    p = next(q for q in pads if q["id"] == pid)
    return f"{p['ref']}.{p['label'] or p['pin']}"


def write_markdown(wires):
    L = ["# Perfboard bağlantı planı (otomatik üretildi)", "",
         "Kaynak: `hardware/perfboard/make_plan.py`. Görsel rehber: `hardware/perfboard/perfboard_plan.html`.",
         "Delik koordinatı = (x sütun, y satır), **bileşen tarafından** bakınca sol-üst köşe (1,1).",
         "Tel boyu = delik mesafesi × 2.54 mm + pay + sıyırma.", ""]
    for st in STAGES:
        L += [f"## Adım {st['n']} — {st['title']}", ""]
        L += [f"- {t}" for t in st["intro"]]
        L += [f"- **Test:** {st['test']}", "", "| # | Nereden | Nereye | Delik | Kes | Renk |", "|---:|---|---|---:|---:|---|"]
        for w in [w for w in wires if w["stage"] == st["n"]]:
            L.append(f"| {w['n']} | {pad_label(w['a'])} (x{w['ax']},y{w['ay']}) | "
                     f"{pad_label(w['b'])} (x{w['bx']},y{w['by']}) | {w['holes']} | {w['cut_cm']} cm | {w['cname']} |")
        if st["n"] == 9:
            L += ["| Nereden | Nereye | Renk | Not |", "|---|---|---|---|"]
            L += [f"| {a} | {b} | {c} | {d} |" for a, b, c, d in HARNESS]
        L.append("")
    with open(os.path.join(HERE, "..", "PERFBOARD_BAGLANTI_PLANI.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(L))


if __name__ == "__main__":
    main()
