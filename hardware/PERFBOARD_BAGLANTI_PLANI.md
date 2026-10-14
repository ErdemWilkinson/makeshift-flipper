# Perfboard bağlantı planı (otomatik üretildi)

Kaynak: `hardware/perfboard/make_plan.py`. Görsel rehber: `hardware/perfboard/perfboard_plan.html`.
Delik koordinatı = (x sütun, y satır), **bileşen tarafından** bakınca sol-üst köşe (1,1).
Tel boyu = delik mesafesi × 2.54 mm + pay + sıyırma.

## Adım 1 — İskelet: soketler + 3V3/GND

- C6'yı iki dişi 1×16 şeride, ekranı 2×(1×20) ERKEK şeride tak; şeritleri modüle takılıyken plakaya dizip öyle lehimle (aralık kendiliğinden doğru olur).
- Sadece güç: C6 3V3 → ekran VCC (VSYS pini), GND hattı.
- Ekranı pin-1 ucu ÜSTTE olacak şekilde tak.
- **Test:** Güç VERMEDEN multimetre süreklilik: 3V3 ↔ GND bipleme­meli. Sonra sadece C6'yı USB ile tak: açılıyor mu?

| # | Nereden | Nereye | Delik | Kes | Renk |
|---:|---|---|---:|---:|---|
| 1 | J1.3V3 (x11,y3) | LCD.VCC · VSYS (x32,y4) | 21.0 | 7.5 cm | kırmızı |
| 2 | J3.GND (x20,y3) | LCD.GND · GND (x32,y5) | 12.2 | 5.0 cm | siyah |

## Adım 2 — Ekran sinyalleri (6 tel)

- SPI + kontrol hatları: CLK, DIN, CS, DC, RST, BL.
- Teller birbirini çaprazlar: bu normal (iki konnektör karşılıklı duruyor).
- **Test:** Mevcut firmware'i yükle: menü ekranda görünmeli.

| # | Nereden | Nereye | Delik | Kes | Renk |
|---:|---|---|---:|---:|---|
| 3 | J3.IO21 (x20,y9) | LCD.BL · GP13 (x25,y19) | 12.4 | 5.0 cm | beyaz |
| 4 | J3.IO9 (x20,y13) | LCD.CS · GP9 (x25,y14) | 5.1 | 2.9 cm | yeşil |
| 5 | J1.IO8 (x11,y11) | LCD.DC · GP8 (x25,y13) | 14.1 | 5.5 cm | mavi |
| 6 | J3.IO20 (x20,y10) | LCD.RST · GP12 (x25,y18) | 10.5 | 4.5 cm | mor |
| 7 | J3.IO19 (x20,y11) | LCD.DIN · GP11 (x25,y17) | 8.7 | 3.9 cm | turuncu |
| 8 | J3.IO18 (x20,y12) | LCD.CLK · GP10 (x25,y16) | 7.0 | 3.4 cm | sarı |

## Adım 3 — Joystick + A (5 tel)

- UP, DOWN, LEFT, RIGHT, A. Hepsi ekran modülünün üstünde, ortak GND'si zaten bağlı.
- **Test:** Menüde UP/DOWN/RIGHT/LEFT/A ile gez.

| # | Nereden | Nereye | Delik | Kes | Renk |
|---:|---|---|---:|---:|---|
| 9 | J1.IO10 (x11,y12) | LCD.A · GP15 (x25,y22) | 17.6 | 6.5 cm | turuncu |
| 10 | J1.IO11 (x11,y13) | LCD.DOWN · GP18 (x32,y19) | 21.9 | 7.8 cm | mavi |
| 11 | J3.IO23 (x20,y7) | LCD.LEFT · GP16 (x32,y22) | 20.0 | 7.3 cm | açık yeşil |
| 12 | J3.IO22 (x20,y8) | LCD.RIGHT · GP20 (x32,y17) | 15.5 | 5.9 cm | kırmızı |
| 13 | J1.IO6 (x11,y7) | LCD.UP · GP2 (x25,y6) | 14.0 | 5.5 cm | açık mavi |

## Adım 4 — Buzzer + titreşim motoru + yük seçici

- JP1'e jumper'ı 1-2'ye tak (USB testi, 3V3).
- IO1 hem buzzer'a hem motor sürücüsünün baz direncine gider.
- D1'in bantlı ucu (katot) M+ tarafına bakar.
- **Test:** Bir menüye girince buzzer ötmeli, titreşim sezilmeli.

| # | Nereden | Nereye | Delik | Kes | Renk |
|---:|---|---|---:|---:|---|
| 14 | J1.3V3 (x11,y3) | JP1.3V3 (x12,y36) | 35.0 | 11.6 cm | kırmızı |
| 15 | Rb2.1 (x15,y29) | BZ1.BZ+ (x24,y29) | 9.0 | 4.0 cm | yeşil |
| 16 | J1.IO1 (x11,y10) | Rb2.1 (x15,y29) | 21.1 | 7.6 cm | yeşil |
| 17 | Rp2.2 (x13,y34) | Q2.E (x14,y34) | 1.0 | 1.7 cm | siyah |
| 18 | Q2.E (x14,y34) | BZ1.BZ− (x25,y29) | 12.1 | 4.9 cm | siyah |
| 19 | J3.GND (x20,y17) | BZ1.BZ− (x25,y29) | 14.4 | 5.6 cm | siyah |
| 20 | Rb2.2 (x15,y33) | Q2.B (x15,y34) | 1.0 | 1.7 cm | gri |
| 21 | Rb2.2 (x15,y33) | Rp2.1 (x13,y30) | 3.6 | 2.5 cm | gri |
| 22 | Q2.C (x16,y34) | D1.A (x17,y34) | 1.0 | 1.7 cm | gri |
| 23 | M1.M− (x18,y34) | D1.A (x17,y34) | 1.0 | 1.7 cm | gri |
| 24 | M1.M+ (x19,y34) | D1.K (x17,y30) | 4.5 | 2.7 cm | pembe |
| 25 | M1.M+ (x19,y34) | JP1.VLOAD (x13,y36) | 6.3 | 3.2 cm | pembe |

## Adım 5 — RC522 portu (6 tel)

- SCK ve MOSI ekranla ORTAK: aynı C6 deliğine ikinci bir tel gelir.
- Port sırası RC522 modülünün sırasıyla aynı olsun ki 8'li Dupont kablo düz takılsın.
- **Test:** hardware_profile.h → BOARD_HAS_RC522 = 1, flashla. RFID menüsünde kart okut: UID.

| # | Nereden | Nereye | Delik | Kes | Renk |
|---:|---|---|---:|---:|---|
| 26 | J1.3V3 (x11,y3) | J_RC522.3.3V (x2,y12) | 13.4 | 5.3 cm | kırmızı |
| 27 | J1.GND (x11,y17) | J_RC522.GND (x2,y10) | 11.9 | 4.9 cm | siyah |
| 28 | J1.IO5 (x11,y6) | J_RC522.MISO (x2,y8) | 9.2 | 4.1 cm | açık mavi |
| 29 | J3.IO19 (x20,y11) | J_RC522.MOSI (x2,y7) | 18.5 | 6.8 cm | turuncu |
| 30 | J1.IO7 (x11,y8) | J_RC522.SDA/CS (x2,y5) | 9.5 | 4.2 cm | yeşil |
| 31 | J1.IO2 (x11,y14) | J_RC522.RST (x2,y11) | 9.5 | 4.2 cm | kırmızı |
| 32 | J3.IO18 (x20,y12) | J_RC522.SCK (x2,y6) | 19.1 | 7.0 cm | sarı |

## Adım 6 — RDM6300 portu + bölücü

- TX (5V seviyesi) → 1k → [IO15 düğümü] → 1k+1k → GND: 5V → 3.3V.
- RDM6300 5V ile beslenir (C6'nın 5V pini).
- **Test:** 125kHz anahtarlığı bobine yaklaştır: ID görünmeli.

| # | Nereden | Nereye | Delik | Kes | Renk |
|---:|---|---|---:|---:|---|
| 33 | J1.5V (x11,y16) | J_RDM.5V (x2,y17) | 9.1 | 4.0 cm | turuncu |
| 34 | J1.GND (x11,y17) | R9.2 (x8,y19) | 3.6 | 2.5 cm | siyah |
| 35 | J_RC522.GND (x2,y10) | J_RDM.GND (x2,y16) | 6.0 | 3.2 cm | siyah |
| 36 | R8.1 (x6,y15) | R9.1 (x8,y15) | 2.0 | 2.0 cm | gri |
| 37 | R7.2 (x4,y19) | R8.2 (x6,y19) | 2.0 | 2.0 cm | turuncu |
| 38 | J3.IO15 (x20,y6) | R8.2 (x6,y19) | 19.7 | 7.2 cm | turuncu |
| 39 | J_RDM.TX (x2,y15) | R7.1 (x4,y15) | 2.0 | 2.0 cm | gri |

## Adım 7 — IR alıcı + IR verici

- VS1838B: OUT → IO4.
- Verici: IO0 → 1k → 2N2222 baz; LED, 33Ω üzerinden VLOAD'a.
- **Test:** IR Öğren ile kumanda kodu yakala; IR Gönderim Testi'ni telefon kamerasıyla izle (mor yanıp söner).

| # | Nereden | Nereye | Delik | Kes | Renk |
|---:|---|---|---:|---:|---|
| 40 | J1.3V3 (x11,y3) | IR_RX.VCC (x7,y2) | 4.1 | 2.6 cm | kırmızı |
| 41 | Rp1.2 (x2,y34) | Q1.E (x3,y34) | 1.0 | 1.7 cm | siyah |
| 42 | J_RC522.GND (x2,y10) | IR_RX.GND (x6,y2) | 8.9 | 4.0 cm | siyah |
| 43 | Q1.E (x3,y34) | Rp2.2 (x13,y34) | 10.0 | 4.3 cm | siyah |
| 44 | J1.IO4 (x11,y5) | IR_RX.OUT (x5,y2) | 6.7 | 3.4 cm | mavi |
| 45 | J1.IO0 (x11,y9) | Rb1.1 (x4,y29) | 22.6 | 8.0 cm | mor |
| 46 | LED1.A (x8,y34) | Rled.2 (x9,y33) | 1.4 | 1.8 cm | gri |
| 47 | Rb1.2 (x4,y33) | Q1.B (x4,y34) | 1.0 | 1.7 cm | gri |
| 48 | Rb1.2 (x4,y33) | Rp1.1 (x2,y30) | 3.6 | 2.5 cm | gri |
| 49 | Q1.C (x5,y34) | LED1.K (x7,y34) | 2.0 | 2.0 cm | gri |
| 50 | Rled.1 (x9,y29) | D1.K (x17,y30) | 8.1 | 3.8 cm | pembe |

## Adım 8 — Pil ölçer + güç girişleri

- Pil bölücü (R1,R2) ve J_VBAT, J_5V portları, kondansatörler.
- En son: MT3608'i yüksüz 5.3V'a ayarla, sonra bağla (docs/planning/ERDEM_SATIN_ALINACAKLAR.md).
- **Test:** Ekranda pil yüzdesi çıkmalı. JP1 jumper'ını 2-3'e al.

| # | Nereden | Nereye | Delik | Kes | Renk |
|---:|---|---|---:|---:|---|
| 51 | J_5V.5V (x2,y21) | C2.+ (x5,y21) | 3.0 | 2.3 cm | turuncu |
| 52 | J_RDM.5V (x2,y17) | J_5V.5V (x2,y21) | 4.0 | 2.6 cm | turuncu |
| 53 | C2.+ (x5,y21) | C1.+ (x5,y25) | 4.0 | 2.6 cm | turuncu |
| 54 | R1.2 (x22,y35) | R2.2 (x24,y35) | 2.0 | 2.0 cm | sarı |
| 55 | R2.2 (x24,y35) | C3.1 (x26,y34) | 2.2 | 2.1 cm | sarı |
| 56 | J1.IO3 (x11,y15) | R1.2 (x22,y35) | 23.9 | 8.4 cm | sarı |
| 57 | R9.2 (x8,y19) | C2.− (x7,y21) | 2.2 | 2.1 cm | siyah |
| 58 | R2.1 (x24,y31) | BZ1.BZ− (x25,y29) | 2.2 | 2.1 cm | siyah |
| 59 | C2.− (x7,y21) | C1.− (x7,y25) | 4.0 | 2.6 cm | siyah |
| 60 | J_VBAT.GND (x18,y36) | Q2.E (x14,y34) | 4.5 | 2.7 cm | siyah |
| 61 | J_5V.GND (x2,y22) | C2.− (x7,y21) | 5.1 | 2.9 cm | siyah |
| 62 | R2.1 (x24,y31) | C3.2 (x26,y36) | 5.4 | 3.0 cm | siyah |
| 63 | J_VBAT.VBAT (x17,y36) | JP1.VBAT (x14,y36) | 3.0 | 2.3 cm | pembe |
| 64 | J_VBAT.VBAT (x17,y36) | R1.1 (x22,y31) | 7.1 | 3.5 cm | pembe |

## Adım 9 — Güç zinciri (modüller arası, plaka dışı)

- Bu adım plakada delik değil, modüller arası kablolar. Sıra önemli: önce MT3608'i ayarla, sonra bağla.
- MT3608'i hiçbir şeye bağlamadan, pile bağlayıp çıkışı multimetreyle ölç ve 5.3V'a getir. Trimmer yönü satıcıya göre ters olabilir: küçük adımlarla çevir, ölçerek yönü bul.
- TP4056 korumalı olmalı (B+/B− ve ayrıca OUT+/OUT− pedleri var). Yük OUT pedlerinden alınır, pil B pedlerine bağlanır.
- Şarj ederken cihazı kapat; karta USB takmadan önce anahtarı kapat.
- **Test:** Anahtar açık, USB yok: ekran açılmalı, pil yüzdesi görünmeli. JP1 jumper'ı 2-3'e alınır.

| # | Nereden | Nereye | Delik | Kes | Renk |
|---:|---|---|---:|---:|---|
| Nereden | Nereye | Renk | Not |
|---|---|---|---|
| LiPo (+) | TP4056 B+ | kırmızı | JST kablosu |
| LiPo (−) | TP4056 B− | siyah |  |
| TP4056 OUT+ | PPTC 1.1A → anahtar giriş | kırmızı | sigorta ile anahtar arasına |
| anahtar çıkış | J_VBAT.VBAT (plaka) + MT3608 VIN+ | kırmızı | bu hat = anahtardan sonraki pil hattı (3.0–4.2V) |
| TP4056 OUT− | J_VBAT.GND (plaka) + MT3608 VIN− | siyah |  |
| MT3608 VOUT+ | 1N5819 anot | turuncu | diyot USB'ye geri beslemeyi önler |
| 1N5819 katot (bantlı uç) | J_5V.5V (plaka) | turuncu | 1000µF C1 zaten bu hatta |
| MT3608 VOUT− | J_5V.GND (plaka) | siyah |  |
| JP1 jumper | pin 2-3 (VLOAD = VBAT) | — | USB testinde 1-2 idi |
