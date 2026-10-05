# Delikli Plaka (Perfboard) Lehim Rehberi — Yeni Başlayanlar İçin

Bu rehber, elindeki hazır modülleri bir **delikli pertinaksa** kalıcı olarak
lehimlemen içindir. PCB ürettirme / KiCad / Gerber YOK — sadece plaka, header
soketleri ve tel.

> **Altın kural:** Önce her şeyi **breadboard'da** çalıştır. Ekranda UID
> görünüyor, buzzer ötüyor, IR öğreniyorsa — ANCAK O ZAMAN pertinaksa lehimle.
> Lehimden sonra hata bulmak 10 kat zordur.

Pin haritasının tamamı: [`../WIRING_MAP_RC522.md`](../WIRING_MAP_RC522.md)

---

## 0. Temel mantık: modülü lehimleme, SOKETİ lehimle

Modülleri (C6, RC522...) doğrudan plakaya lehimleme. Bunun yerine:

1. Plakaya **dişi pin header** (soket) şeridi lehimlersin.
2. Modülü o sokete **takarsın** (lehim yok — sökülebilir).
3. Modül bozulursa/değişirse çıkarıp yenisini takarsın.

Böylece pahalı modüller güvende kalır, sadece ucuz soketler ve teller lehimli olur.

---

## 1. Gerekli aletler ve malzeme (sende var)

- Delikli pertinaks (5x10 veya 10x10 cm)
- Dişi pin header şeritleri (2.54mm, kırılabilir)
- Havya + lehim teli, yan keski, üçüncü el
- Tek damarlı ince tel (jumper içinden çıkan bakır da olur) — alttan bağlantı için
- Multimetre (süreklilik/kısa devre kontrolü için — ÇOK önemli)

---

## 2. Yerleşim planı (lehimden ÖNCE kağıt üstünde)

Modülleri plakaya dizmeden önce yerlerini planla. Öneri:

```
+----------------------------------------------------+
|  [ EKRAN + JOYSTICK ]        [ C6 DEV-KIT ]        |   <- üst sıra
|   (Pico-LCD-1.3)              (ana beyin)          |
|                                                    |
|  [ RC522 ]      [ RDM6300 ]     [ IR alıcı ][IR LED]|  <- orta sıra
|                                                    |
|  [ Buzzer ]  [ R1 R2 bölücü ]  [ pil konnektörü ]  |  <- alt sıra
+----------------------------------------------------+
```

- **C6'yı ortaya/üste koy** — en çok bağlantı ondan çıkar, merkezi olsun.
- Ekranı C6'ya yakın koy (SPI hatları kısa olsun).
- Güç parçaları (pil, MT3608, TP4056) bir köşede toplansın — yüksek akım oradan.

**İpucu:** Modülleri plakaya kuru kuru diz, kalemle soket yerlerini işaretle,
sonra soketleri lehimle.

---

## 3. Lehim sırası (adım adım, test ederek)

Her modülü lehimle → breadboard'da çalıştığını zaten biliyorsun → tak → **bir
sonrakine geç.** Hepsini birden lehimleyip sonra "çalışmıyor" deme.

### Adım A — Güç ve toprak hatları (ÖNCE bu)
Plakanın bir kenarına **+3V3**, **+5V**, **GND** için birer "bara" (uzun tel
şeridi) çek. Bütün modüller güçlerini buradan alacak. GND en önemlisi — her
modülün GND'si aynı hatta gelmeli (**ortak toprak**).

### Adım B — C6 soketi
C6 dev-kit için iki sıra dişi header lehimle. C6'yı tak. USB'den güç ver,
ekranı henüz bağlamadan bootladığını gör (seri log).

### Adım C — Ekran (LCD)
LCD zaten breadboard'da çalışıyordu. Bağlantılar:
| LCD | → C6 |
|---|---|
| SCK | IO18 |
| MOSI | IO19 |
| CS | IO9 |
| DC | IO8 |
| RST | IO20 |
| BL | IO21 |
| VCC | 3V3 |
| GND | GND |

Joystick/butonlar (aynı LCD modülünde):
| Buton | → C6 |
|---|---|
| UP | IO6 |
| DOWN | IO11 |
| LEFT | IO23 |
| RIGHT | IO22 |
| A | IO10 |

Tak, aç → menüde gezebiliyorsan ekran+kontrol tamam.

### Adım D — Buzzer (aktif)
En kolayı, iki tel:
- Buzzer **(+)** → **IO1**
- Buzzer **(−)** → **GND**

Sürücü/diyot GEREKMEZ (aktif buzzer). Bir işlem yapınca "biiip" öterse tamam.

### Adım E — RC522 (13.56MHz NFC)
| RC522 | → C6 | Not |
|---|---|---|
| SCK | IO18 | **ekran SCK'siyle ORTAK** — aynı bara |
| MOSI | IO19 | **ekran MOSI'siyle ORTAK** |
| MISO | IO5 | sadece RC522 |
| SDA (CS) | IO7 | |
| RST | IO2 | |
| 3.3V | 3V3 | **5V DEĞİL** |
| GND | GND | |

Sonra `main/hardware_profile.h` içinde `BOARD_HAS_RC522` değerini **1** yap,
flash'la. Kart okutunca UID görünmeli.

### Adım F — RDM6300 (125kHz)
- Modül **5V** ile beslenir (VCC → 5V bara).
- TX çıkışı **5V** seviyesinde → doğrudan IO15'e bağlama, C6'yı zorlar.
- Araya **level shifter** koy: RDM6300 TX → shifter HV → shifter LV → **IO15**.
  (Alternatif alışveriş notundaki 3×1kΩ bölücü de olur ama shifter daha temiz.)
- Shifter'ın HV tarafı 5V, LV tarafı 3.3V beslenir; ortak GND.

### Adım G — IR alıcı (VS1838B)
Üç bacak: VCC → 3V3, GND → GND, OUT → **IO4**. IR öğren menüsünde uzaktan
kumandayla test et.

### Adım H — IR verici (LED, sürücülü)
LED'i doğrudan pine bağlama, transistörle sür:
```
IO0 ──[1kΩ]── 2N2222 base
                2N2222 collector ── IR LED (katot)
                2N2222 emitter ── GND
IR LED (anot) ──[33Ω]── 3V3
```
(Base-emiter arasına 10kΩ pull-down koymak iyi olur — istenmeyen açılmayı önler.)

### Adım I — Pil bölücü (IO3) — EN SON, pil aşamasında
LiPo 4.2V, C6 ADC max 3.3V → 2:1 bölücü şart:
```
PIL(+) ──[R1]──┬── IO3
               │
             [R2]
               │
GND ───────────┴── GND
```
- **R1 = R2** olmalı. Alışveriş listendeki **1kΩ** dirençlerden 2 tane
  kullanabilirsin (1k+1k de 2:1 böler), ya da 10k+10k. İkisi eşit olsun yeter.
- Bölücü bağlanınca ekrandaki `--` yerine pil yüzdesi çıkar.

---

## 4. Yeni başlayan için lehim ipuçları

- **Havyayı önce ısıt**, ucunu nemli süngerle temizle, ince bir "kalay filmi"
  bırak (tinning). Kuru/kirli uç lehim tutmaz.
- **Isıt + lehim ver, sırayla:** havyayı hem pede hem bacağa değdir (~2 sn),
  sonra lehim telini EKLEME NOKTASINA değdir (havyaya değil). Lehim akınca çek.
- **İyi lehim = parlak, konik (küçük volkan).** Mat, top gibi yuvarlak, ya da
  çatlak görünen = "soğuk lehim", tekrar ısıt.
- **Köprü (bridge) oldu mu?** İki komşu ped yanlışlıkla lehimle birleşti →
  havyayla ısıtıp lehim emici/fitil ile al. Multimetre ile komşu pinler arası
  **süreklilik (kısa devre) kontrolü** yap.
- **Polariteye dikkat:** LED (uzun bacak +), elektrolitik kondansatör (bantlı
  taraf −), diyot (bantlı taraf katot), pil. Ters takarsan parça yanar.

---

## 5. Lehimden sonra — açmadan ÖNCE kontrol

Multimetreyi "süreklilik" (diyot/ses) moduna al:
1. **+3V3 ile GND arası kısa devre var mı?** Ses çıkmamalı. Çıkıyorsa köprü var,
   ara. (Güç vermeden bul!)
2. **+5V ile GND arası** — aynı kontrol.
3. Her modülün VCC'si doğru baraya mı gidiyor (RC522=3V3, RDM6300=5V)?
4. Ancak bu 3 kontrol temizse **güç ver.**

İlk açılışta önce **sadece C6** (ekran görünür), sonra modülleri sırayla test et.
Bir şey çalışmazsa: o modülün GND'si, VCC'si ve sinyal teli — üçünü de multimetre
ile pinden pine kontrol et.

---

## 6. Güç zinciri (en son, pil ile taşınabilirlik)

Alışveriş dosyandaki zincire uy — özellikle:
- **MT3608'i bağlamadan önce yüksüz 5.3V'a ayarla** (multimetreyle). Fabrikadan
  çok yüksek gelebilir, karta öyle bağlarsan yakarsın.
- Şarj ederken **cihaz kapalı** olsun (temel TP4056 aynı anda ikisini yapamaz).
- Karta USB takmadan (flash) önce güç **anahtarını kapat**.

Detaylı güç zinciri: [`../docs/planning/ERDEM_SATIN_ALINACAKLAR.md`](../docs/planning/ERDEM_SATIN_ALINACAKLAR.md)
bölümündeki "Güç zinciri" ve "Güç kuralları".
