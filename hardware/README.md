# Makeshift Flipper — Donanım / PCB

Bu klasör, firmware'deki pin haritasından türetilmiş PCB üretim girdilerini
içerir. Amaç: kablolamayı bir dosyaya döküp, sonunda **lehimli bir PCB**
ürettirmek (JLCPCB / PCBWay gibi bir servise).

## Dosyalar

- **`makeshift_flipper.net`** — KiCad netlist (bağlantı listesi). Hangi
  bileşenin hangi pininin nereye bağlandığını tanımlar. Kaynağı doğrudan
  firmware: `display.c`, `buttons.c`, `rc522.c`, `rdm6300.c`, `ir_driver.c`,
  `vibration.c`, `battery.c`. **Elle tekrar pin girmene gerek yok.**

## Üretim akışı (netlist → lehimli kart)

```
makeshift_flipper.net   (ELİMİZDE ✅)
        ↓  KiCad'e import et
   Şematik (Eeschema)
        ↓  her bileşene footprint ata (çoğu netlist'te yazılı)
   PCB layout (Pcbnew) — parçaları diz, hatları çiz  ← insan işi, en uzun adım
        ↓  Plot / Fabrication Outputs
   Gerber (.gbr) + Drill (.drl) + BOM (.csv) + Pick&Place (.csv)
        ↓  zip'le, servise yükle
   Lehimli PCB gelir 📦
```

### KiCad'e nasıl aktarılır
1. KiCad'i kur (ücretsiz, kicad.org).
2. Yeni proje aç → Eeschema (şematik editörü).
3. Netlist'i içe aktar: bazı KiCad sürümlerinde **Tools → Update Schematic
   from Netlist**, ya da netlist'i referans alarak bileşenleri elle yerleştir.
   (Not: KiCad'in "asıl" akışı şematiği çizip netlist'i ONDAN üretmektir; bu
   dosya ters yönde bir başlangıç iskeletidir — bileşen ve bağlantı listesini
   hazır verir, footprint'leri doğrulaman gerekir.)
4. Footprint'leri kontrol et (netlist'te `footprint` alanları öneri olarak
   var; kendi parçalarının paketiyle eşleştir).
5. **Update PCB from Schematic** → Pcbnew'de parçalar ratsnest ("lastik bant"
   bağlantılar) ile gelir.
6. Parçaları yerleştir, hatları çiz (autorouter veya elle).
7. **File → Fabrication Outputs → Gerbers + Drill**, ayrıca BOM ve
   Pick&Place CSV'lerini dışa aktar.

## Parça listesi (BOM)

| Ref | Parça | Adet | Not |
|---|---|---|---|
| U1 | ESP32-C6-DEV-KIT-NX | 1 | Ana modül (Waveshare) |
| U2 | RC522 (MFRC522) | 1 | 13.56MHz NFC, **3.3V** |
| U3 | RDM6300 | 1 | 125kHz RFID, **5V** |
| U4 | VS1838B | 1 | IR alıcı, 3.3V |
| U6 | 5V→3.3V level shifter (BSS138 tipi) | 1 | RDM6300 TX → IO15 için |
| Q1 | NPN transistör (2N2222 vb.) | 1 | IR LED sürücüsü |
| D1 | IR LED 940nm | 1 | IR verici |
| R3 | 100Ω | 1 | IR LED seri direnci (LED'e göre ayarla) |
| R4 | 100Ω | 1 | Q1 base direnci (IO0'dan) |
| BZ1 | Aktif buzzer | 1 | + → IO1, − → GND. Sürücü/diyot gerekmez |
| R1 | 10kΩ | 1 | Pil bölücü üst |
| R2 | 10kΩ | 1 | Pil bölücü alt (R1==R2 → 2:1) |
| U5 | ST7789 1.8" 240x240 LCD | 1 | SPI, SCK/MOSI ortak |
| J1 | JST-PH 2 pin | 1 | LiPo pil girişi |

## Kritik kurallar (lehim/tasarım)

- **IO12/IO13 ve IO16/IO17'ye ASLA dokunma** (USB ve CH343 konsol).
- **SCK(IO18) ve MOSI(IO19) ortak hat**: LCD ile RC522 aynı iki hattı
  paylaşır. PCB'de tek bir yol çekip ikisine de dağıt.
- **RC522 ve LCD 3.3V**, RDM6300 **5V** (TX'i level shifter'dan geçer).
- **Pil doğrudan IO3'e gitmez** — 2:1 bölücüden (R1/R2) geçer, yoksa pini yakar.
- **IR LED ve buzzer**: aktif buzzer doğrudan sürülür; IR LED transistör (Q1)
  üzerinden. Motor/pasif buzzer kullanırsan flyback diyot ekle.

Tam açıklamalı kablolama için: [`../WIRING_MAP_RC522.md`](../WIRING_MAP_RC522.md)
