# RC522 <-> ESP32-C6 Kablolama Haritası

> Lehimlemeye başlamadan önce referans için. Bu dosya sadece dokümantasyondur,
> hiçbir kod veya ayar değiştirmez. Tarih: 2026-09-28.

## Önce: dokunma, olduğu gibi kalsın

Ekranın (ST7789, Pico-LCD-1.3) C6'ya giden kabloları **değiştirilmeyecek**:

| Ekran fonksiyonu | C6 GPIO |
|---|---:|
| SCK | IO18 |
| MOSI | IO19 |
| CS | IO9 |
| DC | IO8 |
| RST | IO20 |
| Backlight (BL) | IO21 |

Bunlara hiç dokunma. RC522'nin SCK ve MOSI hatları aşağıda bu ikisine
**paralel** eklenecek (aynı elektriksel noktaya ikinci bir kablo).

## RC522 -> C6 tam bağlantı tablosu

| RC522 pini | C6 GPIO | Paylaşımlı mı? | Not |
|---|---:|---|---|
| SCK  | **IO18** | Evet (ekranla) | Ekranın SCK kablosuna paralel bağlanacak |
| MOSI | **IO19** | Evet (ekranla) | Ekranın MOSI kablosuna paralel bağlanacak |
| MISO | **IO6**  | Hayır | C6'da tamamen boş, sadece RC522'ye ait |
| SDA / CS | **IO7** | Hayır | C6'da tamamen boş, sadece RC522'ye ait |
| RST  | **IO2**  | Hayır | C6'da tamamen boş, sadece RC522'ye ait |
| 3.3V | **3V3**  | Hayır (ayrı nokta kullanılabilir) | **Kesinlikle 5V değil** |
| GND  | **GND**  | Hayır (ayrı nokta kullanılabilir) | Board'da birden fazla GND noktası varsa herhangi biri olur |

Toplam 7 tel: 2 tanesi paylaşımlı (SCK, MOSI), 5 tanesi RC522'ye özel tek kablo
(MISO, CS/SDA, RST, 3.3V, GND).

## Paylaşımlı iki hat (SCK, MOSI) için lehimleme yöntemi

C6'nın IO18/IO19 pinlerine zaten ekranın kablosu lehimli/takılı. Aynı pine
ikinci bir jumper ucu güvenle oturmayabilir, bu yüzden **breadboard yoksa**
şu yöntem izlenecek:

1. RC522'nin SCK teli ile ekranın SCK teli (C6 IO18 ucundaki serbest kısım)
   birbirine **bükülüp kalaylanarak lehimlenir** -- iki tel tek bir birleşim
   noktası olur.
2. Bu birleşim noktasından tek bir uç C6'nın IO18 pinine lehimlenir/takılır.
3. Aynı işlem MOSI için tekrarlanır: RC522 MOSI teli + ekran MOSI teli
   birleştirilip tek uçla C6 IO19'a bağlanır.

Breadboard varsa daha kolay: ekranın IO18 kablosunu ve RC522'nin SCK kablosunu
aynı breadboard satırına tak (satırdaki tüm delikler elektriksel olarak
birbirine bağlıdır) -- lehim gerekmez. Aynısını MOSI için tekrarla.

## Diğer beş hat (MISO, CS, RST, 3.3V, GND)

Bunlar C6'da tamamen boş pinler, tek bir jumper kablosuyla doğrudan
bağlanır. Paylaşım yok, lehim/breadboard karmaşası yok.

## Bağlama bittikten sonra yapılacak (yazılım tarafı)

Kablo tamamlanıp RC522'nin çalıştığı doğrulanana kadar bu adım
**yapılmayacak**. Hazır olunca [`main/hardware_profile.h`](main/hardware_profile.h)
içindeki şu satır:

```c
#define BOARD_HAS_RC522 0
```

`1` yapılıp firmware yeniden derlenip flashlanacak. Bu satır değişmeden RC522
kodu firmware'de tamamen pasif kalır -- kablo bitmiş olsa bile cihaz onu
kullanmaz.

## Neden bu pinler seçildi (arka plan)

- Joystick UP daha önce IO6'daydı, RC522 MISO da IO6 istiyordu -- çakışma.
  UP, IO13'e taşındı (bu oturumda), IO6 tamamen RC522 MISO'ya ayrıldı.
- IO18/IO19 (SCK/MOSI) zaten C6'da tek SPI bus'ı (SPI2), ekran da onu
  kullanıyor -- C6'da ikinci bir genel amaçlı SPI host yok, bu yüzden RC522
  ekranla aynı hattı paylaşmak zorunda. Bu tasarım gereği, hata değil.
- IO7 (CS) ve IO2 (RST) RC522'nin kendi ayrı sinyalleri, hiçbir şeyle
  paylaşılmıyor, hep boştular.
