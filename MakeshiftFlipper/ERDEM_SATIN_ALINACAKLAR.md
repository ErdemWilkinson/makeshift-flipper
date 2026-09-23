# C6-Standalone Alışveriş Listesi

Son kontrol: **23 Eylül 2026**. Fiyat ve stoklar değişebilir. Bu liste,
fotoğraflarda görünen parçaları yeniden satın aldırmadan prototipi kurmak için
eksik kalanları kapsar.

## Kısa karar

Bu liste tamamlandığında cihaz elektriksel olarak kurulabilir hale gelir ve
masa testine başlayabiliriz. Yine de parçaların gelmesi tek başına "kesin
çalışıyor" kanıtı değildir. Son onay; firmware derlemesi, ilk enerjilendirme,
LCD/TCA9554, ortak SPI, RFID, IR, motor ve Wi-Fi/BLE stres testlerinden sonra
verilir.

## Sende zaten var, tekrar alma

- Waveshare ESP32-C6-Pico, 4 MB
- Waveshare Pico-LCD-1.3, 240x240 ST7789
- RC522 ve kart/anahtarlık seti
- RDM6300 125 kHz okuyucu
- 3,3 V / 5 V lojik seviye dönüştürücü
- 3,7 V 1000 mAh LiPo
- TP4056 Type-C şarj kartı
- 6x14 mm titreşim motoru
- M-F ve F-F jumper kabloları
- Arduino Uno başlangıç seti ve breadboard

## Şimdi alınacaklar

| Adet | Parça | Bağlantı | Yaklaşık fiyat | Not |
|---:|---|---|---:|---|
| 1 | MT3608 ayarlanabilir boost | [Robotistan](https://www.robotistan.com/ayarlanabilir-voltaj-yukseltici-kart-step-up-converter) | 46 TL | Sadece RDM6300 hattı için 5,00 V'a ayarlanacak. Bağlamadan önce multimetreyle ölç. |
| 2 | 2N2222, TO-92 NPN | [Direnc.net](https://www.direnc.net/2n2222-transistor-bjt-npn-to-92) | 2 x 1,74 TL | Biri titreşim motoru, biri IR LED sürücüsü. Pin dizilimini gelen parçanın veri sayfasından doğrula. |
| 2 | 1N5819 Schottky diyot | [Direnc.net](https://www.direnc.net/1n5819-1a-40v-schottky-rectifier-mic) | 2 x 1,51 TL | Biri motor flyback diyodu, biri yedek. Motor uçlarına ters polaritede bağlanacak. |
| 2 | 5 mm 940 nm IR LED | [Direnc.net](https://www.direnc.net/5mm-940nm-80mw-infrared-led) | Stok sayfasında | Biri kullanım, biri yedek. Başlangıç setinden çıkarsa alma. |
| 1 | Mini SPST ON/OFF anahtar | [Robotistan](https://www.robotistan.com/yuvarlak-mini-anahtar-siyah-onoff-2p) | 5,91 TL | TP4056 çıkışı ile cihaz güç hattı arasına. |
| 1 | 1000 uF / 10 V elektrolitik | [Direnc.net](https://www.direnc.net/1000uf-10v-kondansator-10x16-5mm) | 2,39 TL | C6 VSYS girişine yakın; polariteye dikkat. |
| 1 | 100 uF / 10 V veya 16 V elektrolitik | [Direnc.net](https://www.direnc.net/100-uf) | yaklaşık 1 TL | RDM6300'un 5 V girişine yakın. |
| 1 | 5x10 cm veya 10x10 cm delikli plaket | [Robotistan](https://www.robotistan.com/delikli-pertinaks) | 19-42 TL | Güç, sürücü ve konnektör devresi için. İlk test breadboard üzerinde yapılabilir. |
| 1 | 2 pin erkek-dişi kablolu JST seti | [Motorobit](https://www.motorobit.com/2-pin-jst-kablo-seti-disi-erkek) | 11,88 TL | Çıplak LiPo kablolarına kutuplu ve sökülebilir bağlantı sağlar. Soket tipi PH değilse iki taraf birlikte kullanılır. |

## Dirençler: önce Arduino setini kontrol et

Sette yoksa her birinden birkaç adet 1/4 W al:

- **470 ohm:** motor sürücüsünün 2N2222 baz direnci.
- **1 kohm:** IR LED sürücüsünün 2N2222 baz direnci.
- **10 kohm:** iki sürücünün baz-emiter pull-down direnci.
- **22 ohm ve 33 ohm:** IR LED seri direnci seçenekleri. İlk test 33 ohm ile
  yapılacak; menzil yetersizse akım ölçülerek 22 ohm değerlendirilecek.

Dirençler için: [Robotistan direnç kategorisi](https://www.robotistan.com/direnc).

## Fiziksel duruma göre alınacaklar

### 1. LCD takılınca yan pinlere erişilemiyorsa

- İki adet **1x20, 2,54 mm, uzun bacaklı erkek-dişi stackable header** gerekir.
- Robotistan'daki [Raspberry Pi Pico 1/2 Pin Header](https://www.robotistan.com/header)
  ürününü seçerken pakette iki ayrı 1x20 sıra ve geçişli uzun pin bulunduğunu
  satıcı görselinden doğrula.
- Birleşik Raspberry Pi tipi **2x20 header alma**; iki sıra arasındaki fiziksel
  mesafe Pico kartına uymaz.

### 2. TP4056 kartını kontrol et

Mevcut kartın üzerinde koruma devresi bulunmalı ve pil için `B+/B-`, yük için
ayrı `OUT+/OUT-` uçları olmalı. Bunlar yoksa mevcut kartı kullanma; yerine:

- [Korumalı TP4056 Type-C, Motorobit](https://www.motorobit.com/tp4056-37v-sarj-devresi-korumali-mod-type-c)
  (yaklaşık 30 TL), veya
- [Korumalı TP4056 Type-C, RobotTR](https://www.robottr.com.tr/tp4056-type-c-korumali-sarj-modulu-robot-tr)
  (yaklaşık 22 TL).

Temel TP4056 gerçek bir power-path/load-sharing devresi değildir. Bu
prototipte cihaz **kapalıyken şarj edilecek**; çalışırken USB şarj kablosu
takılmayacak. Pil üreticisinin izin verdiği şarj akımı doğrulanmadan 1 A ile
şarj etmeyeceğiz.

### 3. Ölçü ve lehim ekipmanı yoksa

- Dijital multimetre: [DT-830D](https://www.robotistan.com/dt-830d-dijital-multimetre-sari)
- Isıyla daralan makaron: [Robotistan makaron kategorisi](https://www.robotistan.com/isiyla-daralan-makaron)
- Havya, lehim teli, yan keski ve üçüncü el

Multimetre olmadan boost çıkışı, LiPo polaritesi ve kısa devre kontrolü
yapılmayacağı için pil aşamasına geçilmeyecek.

## Alınmaması gerekenler

- **IRF520 sürücü modülü:** ürün sayfalarında 3,3 V sinyal yazsa da IRF520,
  düşük gerilimli LiPo ve 3,3 V GPIO için iyi bir logic-level seçim değildir.
- **2N7000 motor sürücüsü:** küçük sinyal MOSFET'i; motor kalkış akımı
  ölçülmeden güvenli kabul edilmez.
- Harici analog joystick: Pico-LCD üzerindeki dijital joystick kullanılacak.
- İkinci ESP32 veya ESP32-P4: saha cihazı tek ESP32-C6 ile çalışacak.
- İkinci ekran: Pico-LCD-1.3 aktif ekrandır.

## Tahmini eksik parça bütçesi

- Zorunlu elektronik ve montaj parçaları: yaklaşık **130-180 TL + kargo**.
- TP4056 değişirse: yaklaşık **+22-30 TL**.
- Multimetre yoksa: yaklaşık **+176 TL**.
- Kutu bu aşamada alınmayacak; çalışan prototip ölçüldükten sonra seçilecek
  veya 3B basılacak.

## Parçalar gelince kabul sırası

1. LiPo bağlanmadan C6-Pico kimliği, 4 MB flash ve firmware derlemesi.
2. Yalnız LCD ve üzerindeki joystick/TCA9554 testleri.
3. RC522 ortak SPI testi.
4. MT3608 çıkışını yüksüz 5,00 V'a ayarlayıp RDM6300 testi.
5. IR alıcı ve transistorlü IR verici testi.
6. Motor, 2N2222 ve flyback diyotuyla kısa darbe testi.
7. En son TP4056, LiPo, anahtar ve kondansatörlü güç zinciri.
8. Bütün modüller bağlıyken 30 dakika ekran, RFID ve Wi-Fi/BLE stres testi.

Bu sekiz adım geçmeden kalıcı lehimleme ve kutu tasarımına geçilmeyecek.
