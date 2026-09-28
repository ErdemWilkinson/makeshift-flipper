# Alışveriş Listesi — tek seferde

Son güncelleme: **28 Eylül 2026**. Fiyat ve stoklar değişebilir.
Kart: **Waveshare ESP32-C6-DEV-KIT-NX** (C6-Pico değil). Pin haritası:
[WIRING_MAP_RC522.md](WIRING_MAP_RC522.md).

## Sende zaten var, tekrar alma

- ESP32-C6-DEV-KIT-NX
- Waveshare Pico-LCD-1.3 (ekran + joystick)
- RC522 ve kart/anahtarlık seti
- RDM6300 125 kHz okuyucu
- 3,3 V / 5 V lojik seviye dönüştürücü
- 3,7 V 1000 mAh LiPo
- TP4056 Type-C şarj kartı (aşağıdaki kontrolü yap)
- 6x14 mm titreşim motoru (çıplak, 2 kablolu)
- M-F ve F-F jumper kabloları, 3 adet 1 kΩ direnç
- Arduino Uno başlangıç seti

## Alınacaklar

### Montaj

| Adet | Parça | Not |
|---:|---|---|
| 1 | Breadboard (400 veya 830 delik) | Önce her şey lehimsiz burada test edilecek. |
| 1 | Jumper seti (M-M, M-F, F-F karışık) | |
| 1 | [Delikli pertinaks](https://www.robotistan.com/delikli-pertinaks) 5x10 veya 10x10 cm | Her şey breadboard'da çalıştıktan sonra kalıcı montaj için. |
| 2-3 | Dişi pin header şeridi (1x40, 2,54 mm, kırılabilir) | Modüller pertinaksa lehimlenmez, bu soketlere takılır. |

### Modüller ve sürücüler

| Adet | Parça | Not |
|---:|---|---|
| 1 | VS1838B IR alıcı (3 pinli modül) | IO4. |
| 2 | [5 mm 940 nm IR LED](https://www.direnc.net/5mm-940nm-80mw-infrared-led) | IR verici, biri yedek. Arduino setinde çıkarsa alma. |
| 2 | [2N2222 NPN, TO-92](https://www.direnc.net/2n2222-transistor-bjt-npn-to-92) | Biri motor, biri IR LED sürücüsü. |
| 3 | [1N5819 Schottky diyot](https://www.direnc.net/1n5819-1a-40v-schottky-rectifier-mic) | Biri motor ters diyotu, biri MT3608 çıkışında USB'ye geri beslemeyi önlemek için, biri yedek. |
| 1 | 125 kHz EM4100 anahtarlık | Apartman anahtarlığın varsa alma; RDM6300 testi için. |

### Dirençler (önce Arduino setine bak)

Sette yoksa her birinden birkaç adet 1/4 W ([Robotistan direnç](https://www.robotistan.com/direnc)):

- **470 Ω:** motor 2N2222 baz direnci
- **1 kΩ:** IR LED 2N2222 baz direnci
- **10 kΩ:** iki transistörün baz-emiter pull-down direnci
- **33 Ω ve 22 Ω:** IR LED seri direnci (önce 33 Ω)

### Güç (pil ile taşınabilir kullanım)

| Adet | Parça | Not |
|---:|---|---|
| 1 | [MT3608 boost](https://www.robotistan.com/ayarlanabilir-voltaj-yukseltici-kart-step-up-converter) | **Tüm cihazı** besler. Bağlamadan önce yüksüz **5,3 V**'a ayarla; çıkışındaki 1N5819'dan sonra hat ~4,9 V olur. |
| 1 | 1,1 A PPTC (kendini sıfırlayan sigorta) | TP4056 OUT+ ile anahtar arasına. Kısa devrede akımı keser. |
| 1 | [Mini ON/OFF anahtar](https://www.robotistan.com/yuvarlak-mini-anahtar-siyah-onoff-2p) | Sigorta ile MT3608 girişi arasına. |
| 1 | [1000 µF / 10 V elektrolitik](https://www.direnc.net/1000uf-10v-kondansator-10x16-5mm) | Kartın 5V pinine yakın. Polariteye dikkat. |
| 1 | [100 µF / 10-16 V elektrolitik](https://www.direnc.net/100-uf) | RDM6300'ün 5V girişine yakın. |
| 1 | [2 pin JST kablo seti](https://www.motorobit.com/2-pin-jst-kablo-seti-disi-erkek) | LiPo'yu sökülebilir bağlamak için. |

**TP4056 kontrolü:** Senin kartında `B+ / B-` ve ayrıca `OUT+ / OUT-` uçları
varsa korumalıdır, yenisini alma. Sadece `B+ / B-` varsa korumasızdır;
yerine korumalı olanı al:
[Motorobit](https://www.motorobit.com/tp4056-37v-sarj-devresi-korumali-mod-type-c)
veya [RobotTR](https://www.robottr.com.tr/tp4056-type-c-korumali-sarj-modulu-robot-tr).

### Aletler (yoksa)

- **Dijital multimetre** ([DT-830D](https://www.robotistan.com/dt-830d-dijital-multimetre-sari)).
  Pil aşamasına geçmek için şart.
- [Isıyla daralan makaron](https://www.robotistan.com/isiyla-daralan-makaron)
- Havya, lehim teli, yan keski, üçüncü el

## Alınmayacaklar

- Stackable Pico header: bu kart için gerekmiyor.
- IRF520 veya 2N7000 motor sürücüsü: 3,3 V GPIO için uygun değil.
- İkinci ESP32, ikinci ekran, harici joystick.

## Güç zinciri

```
LiPo → TP4056 (B+/B-) → OUT+ → PPTC 1,1 A → anahtar ─┬→ MT3608 (5,3 V) → 1N5819 → kart 5V pini + RDM6300
                                                     └→ motor ve IR LED (2N2222 üzerinden)
```

- Motor ve IR LED **3.3V pininden değil**, anahtardan sonraki pil hattından
  (3,0-4,2 V) beslenir. Kalkış akımları C6'yı resetlemez, kartın 3.3V
  regülatörüne yük binmez. Breadboard aşamasında (pil yokken) kısa testler
  için geçici olarak 3V3'ten beslenebilir.

## Güç kuralları

- Temel TP4056 aynı anda şarj edip cihazı besleyemez: **cihaz kapalıyken şarj
  et.**
- Karta USB kablosu takmadan (flash vb.) önce **anahtarı kapat.**
- **MT3608 fabrikadan çok yüksek voltajla gelebilir.** Multimetreyle 5,3 V
  ölçülmeden karta bağlanmaz.
- TP4056 koruması pili ~2,4 V'ta keser; LiPo için düşük. Pilin kendi koruma
  kartı var mı kontrol et, pili tamamen bitirme.
- Anahtarı açınca cihaz hemen kapanıyorsa 1000 µF'nin dolma akımı korumayı
  tetiklemiştir; şarj kablosunu takıp çıkarmak sıfırlar. Tekrarlarsa 470 µF
  kullan.

## Parçalar gelince sıra

1. Breadboard'da RC522 (ekranla ortak SPI) → firmware'de açılır, test.
2. RDM6300 (USB'nin 5V'u ile, level shifter veya 3x1 kΩ bölücü) → test.
3. IR alıcı → IR LED + 2N2222 → test.
4. Motor + 2N2222 + 1N5819 → kısa titreşim testi.
5. Hepsi birlikte çalışınca pertinaksa taşı.
6. En son güç zinciri: MT3608'i yüksüz 5,3 V'a ayarla → 1N5819 → PPTC,
   anahtar, TP4056, LiPo. İlk açılışta kartın 5V ve 3V3 pinlerini ölç.
7. Tüm modüller bağlıyken 30 dakika ekran, RFID ve Wi-Fi/BLE testi.
