# Makeshift Flipper — Donanım / PCB

Bu klasör, firmware'deki pin haritasından türetilmiş PCB üretim girdilerini
içerir. Amaç: kablolamayı bir dosyaya döküp, sonunda **lehimli bir PCB**
ürettirmek (JLCPCB / PCBWay gibi bir servise).

## Güncel plan: çıplak yığın (kutusuz), tek perfboard

Güncel mimari: 118×46 mm tek perfboard (46×18 delik), kutu yok, okuma yüzü açık.
RDM6300 bobini + kartı ve RC522 aşağı bakar; üstünde karton / LiPo / karton
sandviçi, elektrik bandıyla sarılı. Plaka ile sandviç arası 2.5 mm hava boşluğu
(perfboard artığından köprü ayaklar), vida yok. Ana README'de özet ve görseller:
[`../README.md`](../README.md#physical-build-the-bare-stack).

| Ne | Dosya |
|---|---|
| 3D model (çarpışma kontrolü, delik planı, montaj animasyonu; mobilde de çalışır) | [`perfboard/bare_stack.html`](perfboard/bare_stack.html) |
| Delik delik plan, 9 adım, her tel numaralı (tarayıcıda aç; "Back (solder) side" kutusu plakayı ters çevirir) | [`perfboard/perfboard_plan_bare.html`](perfboard/perfboard_plan_bare.html) |
| Aynı tel listesi metin olarak | [`PERFBOARD_BAGLANTI_PLANI_BARE.md`](PERFBOARD_BAGLANTI_PLANI_BARE.md) |
| Üstten çizim (parçalar + delikler, telli/telsiz) | [`perfboard/bare_board_top.svg`](perfboard/bare_board_top.svg), [`perfboard/bare_board_top_wires.svg`](perfboard/bare_board_top_wires.svg) |
| Lehimden önce ölçülecekler / ⚠ doğrulanmamışlar | [`perfboard/DOGRULAMA.md`](perfboard/DOGRULAMA.md) |

Tek kaynak `perfboard/make_plan_bare.py`: bir delik koordinatını değiştirip
`python make_plan_bare.py` çalıştırınca plan sayfası, metin listesi ve 3D
sayfadaki plan verisi yeniden üretilir; çizimler için ardından
`python make_board_drawing.py`. Plan sayfası ve çizimler İngilizce, bu klasördeki
notlar Türkçe.

### Eski / karşılaştırma amaçlı sayfalar

Bunlar güncel plan **değildir**, geçmiş denemeler olarak duruyor:
[`perfboard/perfboard_plan.html`](perfboard/perfboard_plan.html) +
[`PERFBOARD_BAGLANTI_PLANI.md`](PERFBOARD_BAGLANTI_PLANI.md) (iki kartlı yığın planı, `make_plan.py`),
[`perfboard/compact_stack.html`](perfboard/compact_stack.html),
[`perfboard/single_board.html`](perfboard/single_board.html) (kabuklu tek board),
[`perfboard/pocket.html`](perfboard/pocket.html) (cebe giren hali). Eski
`LEHIM_REHBERI.md` / `DIYLC_PERFBOARD_REHBERI.md` sadece net listesi veriyordu,
hangi deliğe ne geleceğini vermiyordu.

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
| R7–R9 | 3× 1kΩ (üstte 1k, altta 1k+1k seri) | 3 | RDM6300 TX 5V → ~3.3V, IO15 için. TX'i önce ölç: ~3.3V ise bölücüyü atla. (Level shifter da olur.) |
| Q1 | NPN transistör (2N2222 vb.) | 1 | IR LED sürücüsü |
| D1 | IR LED 940nm (elde: KY-005 modülü) | 1 | IR verici; KY-005'in orta pini belirsiz, önce ölç ([DOGRULAMA](perfboard/DOGRULAMA.md)) |
| R3 | 33Ω (3×100Ω paralel) | 1 | IR LED seri direnci (LED'e göre ayarla) |
| R4 | 1kΩ | 1 | Q1 base direnci (IO0'dan). 100Ω GPIO'yu ~26 mA ile zorlar, 1kΩ kullan |
| BZ1 | Aktif buzzer | 1 | + → IO1, − → GND. Sürücü/diyot gerekmez |
| R1 | 10kΩ | 1 | Pil bölücü üst |
| R2 | 10kΩ | 1 | Pil bölücü alt (R1==R2 → 2:1) |
| U5 | Waveshare Pico-LCD-1.3 (ST7789, 1.3" 240x240) | 1 | SPI, SCK/MOSI ortak; joystick + A dahil |
| J1 | JST-PH 2 pin | 1 | LiPo pil girişi |

## Kritik kurallar (lehim/tasarım)

- **IO12/IO13 ve IO16/IO17'ye ASLA dokunma** (USB ve CH343 konsol).
- **SCK(IO18) ve MOSI(IO19) ortak hat**: LCD ile RC522 aynı iki hattı
  paylaşır. PCB'de tek bir yol çekip ikisine de dağıt.
- **RC522 ve LCD 3.3V** (LCD'nin VSYS pini C6'nın 3V3'üne bağlı; Waveshare aralığı 2.6–5.5V), RDM6300 **5V** (TX'i bölücüden veya level shifter'dan geçer).
- **Pil doğrudan IO3'e gitmez** — 2:1 bölücüden (R1/R2) geçer, yoksa pini yakar.
- **IR LED ve buzzer**: aktif buzzer doğrudan sürülür; IR LED transistör (Q1)
  üzerinden. Motor/pasif buzzer kullanırsan flyback diyot ekle.

Tam açıklamalı kablolama için: [`../WIRING_MAP_RC522.md`](../WIRING_MAP_RC522.md)
