# C6 Standalone Donanim Plani

## Sonuc

Eldeki parcalarla tek kartli saha cihazi mantikli ve yapilabilir. Ana
kart **Waveshare ESP32-C6-Pico (ESP32-C6-MINI-1, 4MB)**, ekran ve giris
birimi **Waveshare Pico-LCD-1.3**, laboratuvar cihazi ise ayri kalan
ESP32-P4-Pico olacak.

Bu plan prototipi kurmaya yeter. Kalici ve guvenli cihaz icin asagidaki
eksik BOM tamamlanmadan pil, motor ve RDM6300 ayni kutuda kullanilmamali.

## Fotograflarla dogrulanan envanter

| Parca | Saha cihazindaki rolu |
|---|---|
| Waveshare ESP32-C6-Pico, 4MB | Ana MCU, Wi-Fi 6, BLE, UI ve cevre birimleri |
| Waveshare Pico-LCD-1.3, ST7789 240x240 | Ekran, 5 yon joystick, A/B/X/Y tuslari |
| RC522 13.56MHz | HF RFID/NFC okuyucu |
| RDM6300 125kHz | LF RFID okuyucu |
| 3.3V-5V seviye donusturucu | RDM6300 TX -> C6 RX korumasi |
| 6x14mm titresim motoru | Haptik geri bildirim; surucu gerekir |
| 3.7V 1000mAh ciplak kablolu LiPo | Ana enerji kaynagi |
| TP4056 Type-C | 1S LiPo sarj; power-path oldugu varsayilmiyor |
| M-F, F-F ve starter kit jumperlari | Prototip baglantilari |
| Arduino Uno starter kit | Breadboard, direnc, BC547, IR seti ve test parcalari |

ESP32-P4-Pico, SSD1306 OLED ve Arduino Uno saha cihazinin zorunlu parcasi
degildir. Harici analog joystick de gereksizdir; ekrandaki joystick ve
tuslar gercek dijital anahtarlardir ve kullanilabilir.

## Kesin pin plani

### LCD ve tuslar

LCD karta Pico basligi uzerinden takilir. Bunlar kart tasariminin sabit
baglantilaridir:

| Islev | Pico pini | ESP32-C6-Pico kaynagi |
|---|---|---|
| LCD SCK | GP10 | GPIO18 |
| LCD MOSI | GP11 | GPIO19 |
| LCD CS | GP9 | GPIO9 |
| LCD DC | GP8 | GPIO8 |
| LCD RST | GP12 | GPIO20 |
| LCD BL | GP13 | GPIO21 |
| Joystick UP | GP2 | GPIO4 |
| Joystick DOWN | GP18 | TCA9554 IO3 |
| Joystick LEFT | GP16 | TCA9554 IO5 |
| Joystick RIGHT | GP20 | GPIO22 / kart ici I2C SDA |
| Joystick PRESS | GP3 | GPIO5 |
| A | GP15 | TCA9554 IO7 |
| B / BACK | GP17 | TCA9554 IO4 |
| X | GP19 | TCA9554 IO2 |
| Y | GP21 | GPIO23 / kart ici I2C SCL |

**Tus kurali:** Joystick cikarilmayacak. UP, DOWN, LEFT, RIGHT, PRESS ve B
kullanilacak; A ve X yedek kalacak; Y kullanilmayacak. RIGHT basiliyken
GPIO22/SDA dusuk kaldigi icin firmware once RIGHT'i dogrudan okuyacak,
basili oldugu surece TCA9554 okumasi yapmayacak ve birakilinca I2C'yi
yeniden yoklayacak. Bu davranis donanimda kabul testinden gecmelidir.

### Harici moduller

| Modul sinyali | C6 GPIO | Pico basligi | Elektrik notu |
|---|---:|---|---|
| RC522 SCK | 18 | GP10 | LCD ile ortak SPI |
| RC522 MOSI | 19 | GP11 | LCD ile ortak SPI |
| RC522 MISO | 6 | GP4 | Yalniz RC522 kullanir |
| RC522 SDA/SS | 7 | GP5 | Ayri CS |
| RC522 RST | 2 | GP27 | Aktif dusuk reset |
| RDM6300 TX | 16 | GP0 | Seviye donusturucu uzerinden RX |
| IR alici OUT | 14 | GP6 | RMT RX |
| IR LED surucu IN | 17 | GP1 | RMT TX; GPIO15 strap pininden kacinilir |
| Motor surucu IN | 3 | GP26 | Motor dogrudan baglanmaz |
| Yedek | 15 | GP7 | Strap pini; harici yuk baglama |
| Yedek | 1 | GP28 | Bos |

RC522 ve LCD ayni SCK/MOSI hatlarini kullanir, fakat CS hatlari ayridir.
Her SPI aygitinin frekansi ayri ayarlanir. Ekran transferi sirasinda RC522
islemi kisa sure bekleyebilir; bu kart taramasi icin kabul edilebilir.

## Guc agaci

```text
USB-C 5V
  -> TP4056
      -> 1S LiPo (B+/B-)
      -> korumali cikis (OUT+/OUT-)
          -> ana ac/kapa anahtari
              +-> C6-Pico VSYS (karttaki MP28164 3.3V uretir)
              +-> 5V boost -> RDM6300 VCC

C6-Pico 3V3 -> RC522 VCC + seviye donusturucunun LV tarafi
RDM6300 5V  -> seviye donusturucunun HV tarafi
Tum GND hatlari ortak
```

- RDM6300 TX, HV girisinden LV cikisina cevrilip GPIO16'ya gider.
- RC522 kesinlikle 3.3V ile beslenir.
- Ciplak LiPo uclarina JST-PH veya kilitli/kutuplu bir soket takilmalidir.
- TP4056 kartinda `B+/B-` ile `OUT+/OUT-` ayri degilse koruma olmayabilir.
- Power-path eklenmezse sarj ederken ana guc kapali tutulmalidir.
- Prototipte C6 USB ile TP4056 USB'yi ayni anda takmamak en guvenli kuraldir.

## Eksik parca hesabi

### Zorunlu

| Adet | Parca | Neden |
|---:|---|---|
| 1 | 5V boost converter, en az 1A sinifi | RDM6300 icin pil geriliminden 5V |
| 1 | SPST ana guc anahtari | Cihazi ve sarj senaryosunu ayirmak |
| 1 | Logic-level N-MOSFET motor surucu veya hazir modul | Motoru GPIO'dan ayirmak |
| 1 | Flyback diyot, SS14/1N5819/1N400x | Motor kesim darbesini bastirmak |
| 1 | Ikinci transistor/MOSFET | IR LED darbeli akim surucusu |
| 1-2 | 940nm IR LED | Kitte yalniz alici + kumanda varsa verici icin |
| 1 | IR LED seri direnci | LED akimini sinirlamak; olcumle secilir |
| 1 | Pico stackable header veya breakout/carrier | LCD takiliyken pinlere erisim |
| 1 | JST-PH 2 pin veya kutuplu pil soketi | Ciplak LiPo'yu guvenli baglamak |

### Guvenilir urun icin kuvvetle onerilen

| Adet | Parca | Neden |
|---:|---|---|
| 1 | LiPo load-sharing/power-path modulu | Calisirken dogru sarj ve sonlandirma |
| 1 | 470-1000uF dusuk ESR kondansator | Wi-Fi/motor gecislerinde VSYS destegi |
| 1 | 100uF kondansator | RDM6300 5V hattini yerel tamponlamak |
| 1 | Delikli plaket veya ozel PCB | Jumper prototipini kalici hale getirmek |
| 1 set | Soket, lehim, makaron, sigorta/PTC | Mekanik ve elektriksel guvenilirlik |
| 1 | Kutu | Pil ve antenleri fiziksel olarak korumak |

Starter kitteki tek BC547 prototipte IR LED icin kullanilabilir. Motorun
kalkis/stall akimi olculmeden BC547 motor surucusu olarak kabul edilmez.

## Gercekci enerji hesabi

1000mAh, 3.7V pilin nominal enerjisi yaklasik **3.7Wh**. Donusturucu ve
kesme kayiplari sonrasi kullanilabilir enerji kabaca 3.0-3.2Wh olur.

| Kullanim | Tahmini ortalama guc | Tahmini sure |
|---|---:|---:|
| UI + RFID, aralikli Wi-Fi/BLE | 0.7-1.1W | 2.8-4.5 saat |
| Yogun Wi-Fi monitor + parlak LCD | 1.2-1.6W | 2.0-2.6 saat |

Bunlar muhendislik tahminidir, olcum degildir. Sonuc; ekran parlakligi,
Wi-Fi TX yogunlugu, boost verimi, pilin gercek kapasitesi ve motor
kullanimina gore degisir.

## Montaj ve kabul sirasi

1. C6-Pico'yu USB ile calistir; flash kimligini ve 4MB kapasiteyi dogrula.
2. Yalniz LCD'yi tak; goruntu, UP/DOWN/LEFT/PRESS/B ve RIGHT I2C kuralini
   test et. Y tusunu devre disi birak.
3. RC522'yi ortak SPI hattina ekle; LCD yenilenirken kart okuma testi yap.
4. RDM6300'u ayri 5V boost ve seviye donusturucu ile ekle.
5. IR aliciyi, sonra transistorlu IR LED vericiyi ekle.
6. Motoru MOSFET + flyback diyot ile ekle; VSYS dusumunu olc.
7. En son LiPo/TP4056/anahtar zincirine gec; once akim sinirli masa
   kaynagiyla polarite ve tuketimi kontrol et.
8. Tum islevler ayni anda acikken en az 30 dakika Wi-Fi, ekran, iki RFID
   okuyucu ve tus stres testi yap. Reset, I2C kilitlenmesi ve kart okuma
   kaybi olmadan gecmeden kalici montaja gecme.

## Karar kapisi

C6-Pico donanim olarak yeterli; GPIO1 gercek genel amacli yedek olarak
kalir. GPIO15 fiziksel olarak bos olsa da strap hatti oldugu icin harici
yuk baglanmamalidir. Projenin en buyuk kalan donanim riski pin sayisi degil,
RIGHT tusunun kart ici I2C SDA ile ortak olmasi ve temel TP4056'nin
load-sharing yapmamasidir. LCD/tus spike'i ve guc olcumu basarili olursa
C6-standalone mimarisiyle devam etmek realistiktir.

## Teknik kaynaklar

- Waveshare ESP32-C6-Pico dokumani: <https://docs.waveshare.com/ESP32-C6-Pico>
- Resmi C6-Pico semasi: <https://files.waveshare.com/wiki/ESP32-C6-Pico/ESP32-C6-Pico-Sch.pdf>
- MP28164 guc entegresi: <https://www.monolithicpower.com/en/products/power-management/switching-converters-controllers/buck-boost/mp28164.html>
- Arduino Uno Pro set icerigi: <https://www.robotistan.com/arduino-tabanli-uno-pro-baslangic-seti>
