# Donanım & Kablolama

> Gerçek, çalışan kablolama. Tüm pinler `main/ui/display.c`,
> `main/input/buttons.c` ve `main/hardware_profile.h` içindeki güncel
> `#define`'lardan alındı (2026-09-27).

## Kartlar

- **MCU kartı:** Waveshare **ESP32-C6-DEV-KIT-NX** (USB-C, iki sıra header,
  her pinin yanında IO numarası yazılı)
- **Ekran:** Waveshare **Pico-LCD-1.3** — ST7789, 240×240, SPI. Üstünde
  5-yön joystick (UP/DOWN/LEFT/RIGHT/CTRL) + A/B/X/Y butonları. Raspberry Pi
  Pico formatında dişi header'ları var.

## Bağlantı yöntemi

Ekran Pico formatlı, C6 kartı değil. **Dişi-dişi (F-F) jumper kablolarla**
tek tek bağlanıyor (breadboard yok). Her kablonun bir ucu C6'nın erkek
pinine, diğer ucu ekranın erkek pinine.

## ⚠️ Güç kuralı (donanım yakmamak için)

- Ekranın **VSYS** pini → C6'nın **3V3** pinine. **ASLA 5V'a bağlama.**
- C6'nın kendi **RST** pinine hiçbir şey bağlanmaz (o C6'yı resetler).
- Kablo değiştirirken **USB'yi çıkar** (güç kesik olsun).

## EKRAN + GÜÇ kablolaması (8 kablo)

| Ekran pini | → C6 pini | İşlev |
|---|---|---|
| VSYS | **3V3** | güç (5V değil!) |
| GND  | **GND** | toprak |
| CLK  | **IO18** | SPI SCK |
| DIN  | **IO19** | SPI MOSI |
| CS   | **IO9**  | LCD chip-select |
| DC   | **IO8**  | data/command |
| RST  | **IO20** | LCD reset |
| BL   | **IO21** | arka ışık |

`display.c` sabitleri: SCK=18, MOSI=19, MISO=6 (paylaşımlı, RC522 için),
CS=9, DC=8, RST=20, BL=21. SPI host = **SPI2_HOST**, 40 MHz.

## BUTON kablolaması (güncel — Codex düzenledi)

`buttons.c` içindeki güncel pinler:

| Ekran butonu | → C6 pini | Yazılım olayı | Not |
|---|---|---|---|
| UP    | **IO6** (`BOARD_UP_GPIO`) | BUTTON_UP | yukarı |
| DOWN  | **IO11** | BUTTON_DOWN | (IO2'den taşındı; IO2 = RC522 RESET idi) |
| LEFT  | **IO23** | BUTTON_BACK | geri/çık |
| RIGHT | **IO22** | BUTTON_RIGHT | giriş/seç |
| CTRL (joystick bas) | **IO3** | BUTTON_PRESS | seç (kablo doğrulanmalı) |
| A     | **IO10** | BUTTON_PRESS | seç |
| B     | — | (devre dışı) | pini DOWN'a verildi |

**Navigasyon sözleşmesi:** RIGHT/PRESS/A = giriş/onay, LEFT = geri,
UP/DOWN = gezinme. Ekran klavyesinde kısa LEFT imleç hareketi, uzun LEFT
geri.

> **Strapping-pin dersi:** Eski düzende DOWN=IO0, PRESS=IO5 idi. IO0/IO5/IO8/
> IO9/IO15 gibi pinler **strapping/boot pinleri**; reset anında basılı
> kalırlarsa cihaz yanlış boot moduna girip **ekranı boş bırakabilir**. Bu
> yüzden butonlar boot-güvenli pinlere taşındı. Yeni pin seçerken bunu
> unutma.

## hardware_profile.h — kritik mimari kararı

Şu an cihazda **sadece LCD + kontrolleri** fiziksel bağlı. `hardware_profile.h`:

```c
#define BOARD_UP_GPIO 6      // joystick UP burada
#define BOARD_HAS_RC522 0    // RC522 şu an KAPALI
```

**Neden RC522 kapalı:** Joystick UP kablosu GPIO6'da. Ama RC522'nin SPI
MISO'su da GPIO6 istiyor. İkisi aynı anda GPIO6'yı kullanamaz. RC522'yi
bağlamak istersen önce UP'ı başka bir boş pine taşıman gerekir (yoksa
`#error` ile derleme durur). Bu bilinçli bir güvenlik kilidi.

## Diğer çevre birimleri (envanterde, tam bağlı değil)

| Modül | C6 pini (kodda) | Durum |
|---|---|---|
| RC522 (13.56MHz) | SCK18/MOSI19/MISO6/CS7/RST2 | **Kapalı** (GPIO6 çakışması) |
| RDM6300 (125kHz) | RX=GPIO1 (UART1) | Kod hazır; **5V + level shifter şart** |
| IR alıcı | GPIO14 (RMT RX) | Kod hazır |
| IR verici | GPIO17 (RMT TX) | Kod hazır |
| Titreşim motoru | GPIO3 civarı | Kod hazır |

### RDM6300 özel notları (kullanıcı sordu)
- **125kHz anten** = bakır tel bobin. İki kablosu beyaz konnektörle
  RDM6300 modülünün **ANT1/ANT2** soketine takılır (C6'ya değil). Kutuplama
  önemsiz.
- **RDM6300 5V ile çalışır, 3.3V ile ÇALIŞMAZ.** VCC → 5V.
- **TX çıkışı 5V seviyesinde** → doğrudan C6'ya bağlanırsa C6 yanar.
  **Level shifter şart:** RDM6300 TX → shifter HV, shifter LV → C6 GPIO1.

## C6-DEV-KIT-NX pin haritası (referans)

Sol sıra (yukarıdan): GND, TXD, RXD, IO15, IO23, IO22, IO21, IO20, IO19,
IO18, IO9, GND, IO13, IO12, GND, NC
Sağ sıra (yukarıdan): 3V3, RST, IO4, IO5, IO6, IO7, IO0, IO1, IO8, IO10,
IO11, IO2, IO3, 5V, GND, NC

Boş/kullanılabilir pinler: IO0, IO1, IO4, IO5, IO7, IO12, IO13, IO15
(bazıları strapping — dikkat).
