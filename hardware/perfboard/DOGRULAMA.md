# Doğrulama notları (perfboard planı + kompakt yığın)

Tarih: 2026-10-14. Parçalar elinde olmadığı için her şey internet kaynaklarına
dayanıyor. **✓ = en az bir kaynakta metin olarak doğrulandı. ⚠ = doğrulanamadı
ya da kaynaklar çelişiyor: kendin ölç.**

## Lehim öncesi ölçülecekler (5 dk, sırayla)

1. ⚠ **C6 şerit arası mesafe.** Planda 9 delik (22.86 mm) varsayıldı. Kartı
   breadboard'a veya iki ayrı şeride tak, iki pin sırası arasındaki delik sayısını say.
   Farklıysa `make_plan.py` içinde `J1X, J3X` değerini değiştirip `python make_plan.py` çalıştır.
2. ⚠ **C6'nın USB-C ucu.** Metinle yayınlanmıyor. Plan "USB ucu aşağı" çiziyor;
   kartına bak, USB-C hangi ucundaysa planı ona göre çevir (anten ucu USB'nin karşısında).
3. ⚠ **Pico sıra aralığı (17.78 mm).** Pico standardı, ama bu aramada metinle
   doğrulayamadım. Ekranın dişi soketinin iki sırası arasını kumpasla ölç
   (17.78 ± 0.2). Erkek header'ı almadan önce.
4. ⚠ **MT3608 trimmer yönü.** Kaynaklar çelişiyor. Çıkışı ayarlamadan hiçbir şeye bağlama:
   pil + girişe bağla, çıkışı multimetreyle ölç, 5.3 V olana kadar küçük adımlarla çevir.
5. ⚠ **Pil boyutu.** 503450 normalde ≈50×34×5–5.5 mm ama bazı 1000 mAh piller 51×30×7.
   Kendi pilini ölç; güç plakası yerleşimi buna göre değişir.
6. ⚠ **RDM6300 başlık pin sayısı.** Kaynaklar 4 ve 5 pin diyor. Kart üzerindeki yazıları
   oku (TX, RX, NC, GND, VCC sırası P1 için doğrulandı).
7. ⚠ **Pil kutup ucu.** JST/kablo renkleri üreticiye göre değişebilir; TP4056 B+/B−
   pedlerine bağlamadan önce multimetreyle bak.

## Doğrulananlar (✓)

| Konu | Sonuç | Kaynak |
|---|---|---|
| Pico-LCD-1.3 | Arkasında hazır lehimli dişi soket var → plakada ERKEK pin | Waveshare wiki, ThePihut |
| LCD yönü | Üzerinde "USB" yazılı uç = Pico USB ucu = pin 1 ucu | Waveshare wiki |
| LCD sinyalleri | DIN GP11, CLK GP10, CS GP9, DC GP8, RST GP12, BL GP13, A GP15, UP GP2, DOWN GP18, LEFT GP16, RIGHT GP20, CTRL GP3, VCC→VSYS | Waveshare wiki |
| C6 pin tabloları | J1/J3 16'şar pin, DevKitC-1 uyumlu; IO12/13 USB, IO16/17 CH343 | Espressif DevKitC-1 kılavuzu, Waveshare wiki |
| RC522 | 3.3 V; header sırası SDA SCK MOSI MISO IRQ GND RST 3.3V | Cirkit Designer, ShillehTek |
| RDM6300 | P1: TX RX NC GND VCC(5 V), P2 anten, 9600 baud | Phipps, Cirkit |
| VS1838B | Çıplak: OUT GND VCC. KY-022 kartı: GND VCC S | ShillehTek, iFuturetech |
| 2N2222 (TO-92) | Düz yüz sana bakarken E-B-C. Metal kasa (TO-18) farklı | components101, microcontrollerslab |
| TP4056 | IN+/IN−, B+/B−, OUT+/OUT−; yük OUT'tan, pil B'den | ShillehTek, Mischianti |
| Dişi header | Gövde 8–8.45 mm, pin kuyruğu 3 mm | Electrokit, ProtoSupplies |
| MT3608 | 37.2×17.2×14 mm | Components101 |

## Kompakt yığın: sonuç

Açmak için: [`compact_stack.html`](compact_stack.html) (3D, patlatma kaydırıcısı, yükseklik bütçesi).

- Taban ≈ **66 × 62 mm**, kalınlık ≈ **67.5 mm** (hesap: sağdaki bütçe tablosu).
- Hacim ≈ 276 cm³; yayılmış 10×10 cm perfboard planı ≈ 220 cm³. **Yığın tabanı
  küçültür, hacmi küçültmez.** Gerçek Flipper boyutu (~100×40×25 mm) için özel PCB şart;
  netlist hazır: [`../makeshift_flipper.net`](../makeshift_flipper.net).
- Tahmin (~) olan bütçe kalemleri: LCD joystick yüksekliği, C6 USB-C yüksekliği,
  RC522 üstü boşluk, güç plakası kalınlığı.
- ⚠ RC522 (13.56 MHz) ile RDM6300 bobini (125 kHz) üst üste ve pil arkada: menzil
  düşebilir. Önce yan yana (perfboard planı) test et, sonra yığ. Anten tarafları
  kasanın dışına (arkaya) baksın, metal pilin hemen üstüne koyma.
- ⚠ Ara parça 18 mm ile C6 üstünde yalnızca ~4.6 mm boşluk var; C6 üstündeki
  en yüksek parça (USB-C ≈ 3.4 mm tahmin) sığar ama pay az. 20 mm ara parça daha güvenli.

## Kaynaklar

- [Espressif ESP32-C6-DevKitC-1 kullanıcı kılavuzu](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html)
- [Waveshare Pico-LCD-1.3 wiki](https://www.waveshare.com/wiki/Pico-LCD-1.3)
- [Waveshare ESP32-C6-DEV-KIT-N8 wiki](https://www.waveshare.com/wiki/ESP32-C6-DEV-KIT-N8)

Tablodaki diğer kaynaklar (Components101, microcontrollerslab, ShillehTek, Phipps,
Cirkit Designer, Electrokit, ProtoSupplies, ThePihut, Mischianti) arama sonuçlarından
okundu; tam adres kaydetmedim, o yüzden link vermiyorum. Başlıkla aratınca bulunur.
