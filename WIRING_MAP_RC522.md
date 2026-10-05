# Kablolama Haritası — ESP32-C6-DEV-KIT-NX

> Lehim yaparken kullanılacak referans. Firmware'deki pin tanımlarıyla
> birebir aynıdır (kaynak: `main/hardware_profile.h`, `display.c`,
> `buttons.c`, `rc522.c`, `rdm6300.c`, `ir_driver.c`, `vibration.c`,
> `battery.c`). Tarih: 2026-09-30.

## Asla kablo bağlama

| Pin | Neden |
|---|---|
| IO12, IO13 | Kartın dahili USB'si (D−/D+). IO13'e UP bağlanınca 1 basış 5 sayıldı ve USB portu kapandı. |
| TXD, RXD (GPIO16/17) | CH343 USB-seri çipine giden konsol hattı. |

## Zaten bağlı olanlar (dokunma)

| Parça | Sinyal | C6 pini | Durum |
|---|---|---|---|
| Ekran | SCK / MOSI | IO18 / IO19 | ✅ Çalışıyor |
| Ekran | CS / DC / RST / BL | IO9 / IO8 / IO20 / IO21 | ✅ Çalışıyor |
| Joystick | UP / DOWN / LEFT / RIGHT | IO6 / IO11 / IO23 / IO22 | ✅ Çalışıyor |
| Buton | A | IO10 | ✅ Çalışıyor |
| Buzzer (aktif) | + ucu | IO1 | ✅ Test edildi ("biiip") |

> **IO3 hakkında:** Firmware'de IO3 iki işe işaretli — eski "joystick orta
> basış" (kullanılmıyor, poll listesinde yok) **ve pil ölçer ADC girişi**
> (`battery.c`). Lehimde IO3'e pil voltaj bölücüsünü bağla (aşağıya bak),
> orta basışı bağlama. İkisi aynı pini paylaşamaz.

## Takılacak modüller

| Modül | Modül pini | C6 pini | Not |
|---|---|---|---|
| RC522 | SCK | IO18 | Ekranın SCK'siyle **ortak hat** |
| RC522 | MOSI | IO19 | Ekranın MOSI'siyle **ortak hat** |
| RC522 | MISO | **IO5** | Sadece RC522 |
| RC522 | SDA (CS) | **IO7** | Sadece RC522 |
| RC522 | RST | **IO2** | Sadece RC522 |
| RC522 | 3.3V / GND | 3V3 / GND | **5V değil.** Ekranın 3.3V/GND noktası da olur |
| RDM6300 | TX | **IO15** | **Mutlaka** 5V→3.3V level shifter üzerinden. Modül 5V ile beslenir |
| IR alıcı (VS1838B) | OUT | **IO4** | 3.3V ile besle |
| IR verici | LED sürücü girişi | **IO0** | Transistör/sürücü üzerinden, LED'i doğrudan pine bağlama |
| Pil ölçer | Bölücü orta ucu | **IO3** | 2:1 voltaj bölücü (aşağıya bak). Doğrudan pil(+) bağlama |

3.3V ve GND güç hatlarıdır. Birden fazla modül aynı noktadan beslenebilir.

> **Buzzer zaten bağlı (IO1).** Sende **aktif** buzzer var (kendi osilatörü
> var, aç/kapa ile öter). Sürücü/diyot gerekmez: buzzer(+) → IO1, buzzer(−) →
> GND. **Pasif** buzzer veya titreşim motoru kullanırsan o zaman transistör +
> ters (flyback) diyot şart olur — motoru/bobini doğrudan pine bağlama.

## Pil ölçer (IO3) — voltaj bölücü

C6'nın ADC girişi en fazla ~3.3V okur; LiPo dolu iken 4.2V'a çıkar. O yüzden
2:1 bölücü şart, yoksa pini yakarsın:

```
PIL(+) ──[ R1 = 10kΩ ]──┬── IO3
                        │
                     [ R2 = 10kΩ ]
                        │
PIL(−)/GND ─────────────┴── GND
```

- R1 = R2 = 10kΩ (2:1 böler; 4.2V → 2.1V, güvenli).
- Orta düğüm (R1 ile R2 arası) → **IO3**.
- Pil(−) ve C6 GND **ortak** olmalı.
- Bölücü yoksa firmware pini boşta (floating) görür ve ekranda `--` gösterir
  — bu bir hata değil, "pil okunmuyor" demek.

## Ortak hatlar (SCK, MOSI) nasıl bağlanır

C6'nın tek SPI hattı ekranla RC522 arasında paylaşılıyor. Bir header
pinine iki jumper güvenle oturmaz, o yüzden:

- **Breadboard varsa:** Ekranın IO18 kablosunu ve RC522 SCK kablosunu aynı
  breadboard satırına tak. MOSI (IO19) için de aynısını yap.
- **Breadboard yoksa:** RC522 SCK telini ekranın SCK teline lehimle, tek uçla
  IO18'e bağla. MOSI için de aynısını yap.

## Pinler neden böyle dağıtıldı

- Kartta boş kalan pinler: IO0, 1, 2, 3, 4, 5, 7, 15.
- IO4, IO5 ve IO15 strap (açılış ayarı) pinleri. Bunlara sadece boşta
  yüksek kalan girişler verildi (RC522 MISO, IR alıcı, RDM6300). Böylece
  reset anında hiçbir modül bu pinleri düşük çekmez.
- Çıkışlar (RC522 CS/RST, IR verici, buzzer) strap olmayan IO0, IO1, IO2 ve
  IO7'ye verildi. Sürücü girişlerinde genelde pull-down olduğu için bunlar
  strap pinlerinde sorun çıkarabilirdi.
- IO3 pil ADC'si için ayrıldı (analog giriş; eski orta-basış kullanılmıyor).

## Takıldıktan sonra

- **RC522:** `main/hardware_profile.h` içinde `BOARD_HAS_RC522` değeri `1`
  yapılıp flashlanmalı. RFID menüsünde kart okutulunca UID görünmeli.
  "Hatalar" menüsünde `RC522_SCAN_ERROR` varsa bağlantılardan biri eksik.
- **RDM6300, IR, buzzer:** Kod her açılışta bu pinleri zaten ayarlıyor, ek
  bir ayar gerekmez. Buzzer zaten test edildi.
- **Pil ölçer:** Bölücü bağlandığında üst banttaki `--` yerine yüzde
  görünmeli. Değer saçmalıyorsa R1/R2 değerlerini ve GND ortaklığını kontrol
  et; okuma tutarsızsa (spread çok geniş) firmware yine `--` gösterir.
