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

8. ⚠ **2N2222A pin sırası.** TO-92'de üreticiye göre değişiyor (PN2222A E-B-C, P2N2222A
   C-B-E; kaynaklar kısmen çelişiyor). Multimetrenin hFE soketine iki yönde tak, yüksek
   okunan yön doğru (E-B-C / C-B-E). Plandaki Q1/Q2 pedlerini buna göre çevir.
9. ⚠ **RDM6300 TX seviyesi.** Hiçbir kaynak sayısal söylemiyor. 5V'a verip TX'i ölç.
   ~5V ise 1k + 2k bölücü kalsın; ~3.3V ise bölücüyü atla (2.2V'a düşer, C6 HIGH sayamayabilir).
10. ⚠ **KY-005 modülü (çıplak LED değil).** Orta pin kaynaklarda GND+direnç, VCC veya boş
    diye çelişiyor. Diyot modunda LED'in iki ucunu bul (düz yönde ~1.0–1.3 V), orta pini
    bağlama. Üzerinde SMD direnç varsa plandaki 33 Ω'u küçült veya çıkar.

## Sipariş karşılaştırması (14 Ekim 2026, ekran görüntülerinden)

| Parça | Siparişte | Plan ihtiyacı | Durum |
|---|---|---|---|
| ESP32-C6-DEV-KIT-NX | görüntülerde yok | 1 | Önceden sende olduğu varsayıldı |
| Pico-LCD-1.3 | 1 | 1 | ✓ |
| RC522 kit, RDM6300, lojik dönüştürücü, titreşim motoru, 1000 mAh LiPo | 1'er | 1'er | ✓ (dönüştürücü planda kullanılmıyor, bölücü var) |
| MT3608 | 1 | 1 | ✓ |
| 1N5819 | 4 | 2 | ✓ |
| 2N2222A | 4 | 2 | ✓, pin sırasını doğrula (#8) |
| VS1838B / TL1838 | 2 | 1 | ✓ |
| IR verici | KY-005 ×2 | çıplak LED varsayılmıştı | ⚠ modül, #10 |
| 1k direnç | 50 | 3 (bölücü) + 1–2 + 1 | ✓ |
| 100R direnç | 50 | 33 Ω yerine 3 paralel | ✓ |
| Perfboard 12×12 ×2, 6×13 ×1 | var | plan 38×38 delik = 9.7×9.7 cm | ✓ 12×12'ye sığar; 6×13'e sığmaz |
| **10k direnç** | **yok** | R1, R2 (pil bölücü), Rp1, Rp2 | ✗ al (ya da geçici 1k+1k: çalışır ama ~2 mA sürekli akıtır) |
| **470 Ω** | yok | Rb2 | 2×1k paralel (500 Ω) |
| **Dişi header 1×16/1×40, erkek header 1×40** | yok | C6 soketi, LCD için erkek | ✗ al |
| **Buzzer (aktif)** | yok | BZ1 | Opsiyonel; motor titreşimi var, soket boş kalabilir |
| TP4056, PPTC 1.1 A, anahtar, 1000/100 µF, JST | görüntülerde yok | güç zinciri | TP4056 sende olduğu belirtilmişti; kalanlar için alışveriş listesine bak |
| ESP32-C6 Geliştirme Kartı 160 MHz (2. fatura, 441 TL) | 1 | ana kart | ⚠ Fatura adı kesik; model (DEV-KIT-NX / C6-Pico / DevKitC) yazmıyor. Pin haritası DevKitC-1 uyumlu başlıklara göre: kartın üstündeki yazıyı kontrol et |
| TP4056 Type-C şarj modülü (2. fatura, 9,8 TL) | 1 | 1 | ⚠ Koruma devresi olup olmadığı belli değil; fiyat korumasız olabileceğini düşündürüyor (çıkarım). OUT+/OUT− pedi var mı bak |
| ESP32-P4-Pico (400 MHz), 0.96" SSD1306 OLED | 1'er | kullanılmıyor | Firmware C6 hedefli; yedek/deney parçası |
| NodeMCU ESP8266 | 1 | kullanılmıyor | Firmware C6 hedefli; yedek/test kartı |

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
