# README Görsel Promptları (P4'süz / C6-standalone)

README'deki `docs/makeshift-flipper-technical-overview.jpg` görselinin yeni,
tek-çip (ESP32-C6) mimarisine uygun karşılığını üretmek için hazırlanmış AI
görsel promptları. Eski görsel iki çipli (P4 + C6) düzeni gösteriyordu; yeni
görselde tek ESP32-C6 hem UI'yı hem radyoyu çalıştırıyor.

Her prompt İngilizce yazıldı (görsel modelleri İngilizce'de daha iyi sonuç
verir). İstediğin stile göre birini seç.

---

## 1. Teknik genel bakış (ana README görseli — eskisinin doğrudan karşılığı)

**Prompt (EN):**
```
Clean technical overview illustration of a handheld "Makeshift Flipper"
security research device, top-down / flat-lay style on a dark slate
background. A single ESP32-C6 microcontroller board sits at the center as
the brain, with thin labeled connection lines radiating out to its
peripherals: a 240x240 square color LCD screen (Waveshare Pico-LCD-1.3 with
a 5-way joystick and buttons), an RC522 13.56MHz NFC reader board, an
RDM6300 125kHz RFID reader board, an infrared receiver and IR LED pair, a
small vibration motor, and a 3.7V LiPo battery with a TP4056 charging
module. Above the ESP32-C6, two subtle radiating wave arcs labeled "Wi-Fi"
and "BLE" show the on-chip radio. Modern flat vector infographic style,
thin white/cyan labels, muted teal and orange accents, no clutter, plenty
of negative space, engineering-diagram aesthetic. No text errors, crisp
labels. 16:9.
```

**Önemli:** Görselde **tek** mikrodenetleyici olmalı (eski görseldeki gibi
iki ayrı çip DEĞİL). Wi-Fi ve BLE dalgaları doğrudan ESP32-C6'dan çıkmalı.

---

## 2. Alternatif — izometrik / 3D stil

**Prompt (EN):**
```
Isometric 3D render of a handmade handheld hacking/RFID research gadget on
a neutral studio background. One ESP32-C6 dev board is the core, wired with
neat colored jumper wires to: a square 1.3 inch 240x240 color LCD with a
directional joystick, an RC522 NFC module, an RDM6300 125kHz RFID module,
an IR receiver and IR LED, a tiny vibration motor, and a small LiPo battery
with a TP4056 USB-C charger board. Exposed-electronics prototype look,
breadboard-and-jumper aesthetic, soft studio lighting, shallow depth of
field. Emphasize that it is ONE compact controller board driving everything,
including on-board Wi-Fi and Bluetooth (small wireless icon near the chip).
Warm accent lighting, clean and modern. 16:9.
```

---

## 3. Alternatif — minimalist blok diyagram (en teknik, en okunaklı)

**Prompt (EN):**
```
Minimalist block-diagram infographic, dark background, for a single-MCU
handheld device. One central rounded rectangle labeled "ESP32-C6 (UI +
radio)". Around it, connected by clean orthogonal lines with small
interface labels, six blocks: "ST7789 240x240 LCD + joystick (SPI)",
"RC522 NFC 13.56MHz (shared SPI)", "RDM6300 125kHz (UART)", "IR RX/TX
(RMT)", "Vibration motor (GPIO)", "LiPo + TP4056 (power)". One block above
labeled "Wi-Fi + BLE" drawn INSIDE or touching the ESP32-C6 box to show it
is on-chip, not a separate module. Flat, high-contrast, cyan and amber on
dark, monospaced-style labels, perfectly aligned, no gradients, no visual
noise. 16:9.
```

---

## Nasıl kullanılır

1. Yukarıdaki promptlardan birini bir görsel üretme aracına ver.
2. Çıkan görseli `docs/makeshift-flipper-technical-overview.jpg` olarak
   kaydet (README zaten bu yolu gösteriyor, dosya adını değiştirmen
   gerekmez).
3. Üretilen görselde etiketlerin doğru yazıldığından ve **tek** MCU
   göründüğünden emin ol — görsel modelleri bazen metni bozar veya fazladan
   çip ekler; öyleyse yeniden üret.

## İpuçları
- "single MCU", "one ESP32-C6", "on-chip Wi-Fi/BLE" ifadelerini prompt'ta
  vurgulaman, modelin eski iki-çipli düzeni tekrar üretmesini engeller.
- Etiket metni bozulursa, promptun sonuna "clean legible labels, no
  gibberish text" ekle.
- Tutarlı bir marka görünümü istersen üç promptta da aynı renk paletini
  (teal + amber, koyu arka plan) kullan.
