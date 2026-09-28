# Kablolama Haritası — ESP32-C6-DEV-KIT-NX

> Lehim yaparken kullanılacak referans. Firmware'deki pin tanımlarıyla
> birebir aynıdır (kaynak: `main/hardware_profile.h`, `display.c`,
> `buttons.c`, `rc522.c`, `rdm6300.c`, `ir_driver.c`, `vibration.c`).
> Tarih: 2026-09-28.

## Asla kablo bağlama

| Pin | Neden |
|---|---|
| IO12, IO13 | Kartın dahili USB'si (D−/D+). IO13'e UP bağlanınca 1 basış 5 sayıldı ve USB portu kapandı. |
| TXD, RXD (GPIO16/17) | CH343 USB-seri çipine giden konsol hattı. |

## Zaten bağlı olanlar (dokunma)

| Parça | Sinyal | C6 pini |
|---|---|---|
| Ekran | SCK / MOSI | IO18 / IO19 |
| Ekran | CS / DC / RST / BL | IO9 / IO8 / IO20 / IO21 |
| Joystick | UP / DOWN / LEFT / RIGHT | IO6 / IO11 / IO23 / IO22 |
| Buton | A | IO10 |
| Joystick | Orta basış (kablosu doğrulanmadı) | IO3 |

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
| Titreşim motoru | Sürücü girişi | **IO1** | Sürücü + motora ters diyot şart, motoru doğrudan pine bağlama |

3.3V ve GND güç hatlarıdır. Birden fazla modül aynı noktadan beslenebilir.

## Ortak hatlar (SCK, MOSI) nasıl bağlanır

C6'nın tek SPI hattı ekranla RC522 arasında paylaşılıyor. Bir header
pinine iki jumper güvenle oturmaz, o yüzden:

- **Breadboard varsa:** Ekranın IO18 kablosunu ve RC522 SCK kablosunu aynı
  breadboard satırına tak. MOSI (IO19) için de aynısını yap.
- **Breadboard yoksa:** RC522 SCK telini ekranın SCK teline lehimle, tek uçla
  IO18'e bağla. MOSI için de aynısını yap.

## Pinler neden böyle dağıtıldı

- Kartta boş kalan pinler tam 7 tane (IO0, 1, 2, 4, 5, 7, 15) ve 7 sinyal
  gerekiyor.
- IO4, IO5 ve IO15 strap (açılış ayarı) pinleri. Bunlara sadece boşta
  yüksek kalan girişler verildi (RC522 MISO, IR alıcı, RDM6300). Böylece
  reset anında hiçbir modül bu pinleri düşük çekmez.
- Çıkışlar (RC522 CS/RST, IR verici, motor) strap olmayan IO0, IO1, IO2 ve
  IO7'ye verildi. Sürücü girişlerinde genelde pull-down olduğu için bunlar
  strap pinlerinde sorun çıkarabilirdi.

## Takıldıktan sonra

- **RC522:** `main/hardware_profile.h` içinde `BOARD_HAS_RC522` değeri `1`
  yapılıp flashlanmalı. RFID menüsünde kart okutulunca UID görünmeli.
  "Hatalar" menüsünde `RC522_SCAN_ERROR` varsa bağlantılardan biri eksik.
- **RDM6300, IR, motor:** Kod her açılışta bu pinleri zaten ayarlıyor, ek
  bir ayar gerekmez.
