# DIYLC ile Perfboard Tasarımı — Adım Adım

Lehimden ÖNCE, delikli plakada modül yerleşimini ve telleri ekranda planlamak
için. Araç: **DIYLC (DIY Layout Creator)** — perfboard planlamanın en kolay yolu.

> Java 8 sende kurulu (kontrol edildi). DIYLC çalışır.

---

## 1. Kurulum

1. Tarayıcıda: **github.com/bancika/diy-layout-creator**
2. Sağdaki **"Releases"** → en son sürüm → Windows `.zip`'ini indir.
3. Zip'i bir klasöre çıkar (örn. `C:\DIYLC`).
4. İçindeki **`diylc.exe`** ya da **`run.bat`** ile aç.
   (Açılmazsa: `run.bat`'a sağ tık → düzenle → Java yolu doğru mu bak.)

---

## 2. Yeni perfboard oluştur

1. **File → New** ile boş sayfa gelir.
2. Sol paneldeki bileşen kategorilerinden **"Boards"** → **"Perf Board"**
   (delikli plaka) seç, sayfaya bırak.
3. Perf board'a tıkla → sağ/alt özellik panelinden **delik sayısını** ayarla.
   Çekirdek için **~20 x 15 delik** yeter (sonra büyütürsün).
4. Delik aralığı zaten **0.1 inch (2.54mm)** — gerçek perfboard ile birebir.

---

## 3. Çekirdek yerleşimi (C6 + Ekran + Joystick + Buzzer)

Önce SADECE çalışan çekirdeği planla. RFID/IR/pil sonra eklenir.

### Modülleri temsil etmek
DIYLC'de C6 dev-kit / ekran için birebir hazır parça olmayabilir. İki yol:
- **Kolay yol:** "Misc → Rectangle" ya da "Connectivity → Header/Pin Header"
  ile modülü temsil eden bir dikdörtgen/pin sırası koy, üstüne adını yaz
  (sağ panelden "Name" = "ESP32-C6", "LCD" vb.).
- Modüller plakaya **dişi header** ile oturacağı için, her modülün altına bir
  **pin header** sırası koymak gerçeğe en yakın gösterim olur.

### Yerleşim önerisi (kuşbakışı)
```
   +-------------------------------------------+
   |  [ EKRAN + JOYSTICK ]     [  ESP32-C6  ]  |
   |   (Pico-LCD-1.3)          (dev-kit)       |
   |                                           |
   |  [ Buzzer ]                               |
   +-------------------------------------------+
```
- C6'yı sağa/ortaya koy — en çok tel ondan çıkar.
- Ekranı C6'ya yakın koy (SPI telleri kısa olsun).
- Buzzer küçük, bir köşeye.

---

## 4. Telleri çiz (çekirdek — 15 bağlantı)

DIYLC'de tel için: sol panel **"Connectivity" → "Hookup Wire"** (ya da "Jumper").
Bir ucu bir modülün pinine, diğer ucu C6'nın ilgili pinine bırak. Renk seç
(güç kırmızı, GND siyah, sinyal başka renk — karışmasın).

### Bağlantı listesi (bunu birebir çiz)

**Güç:**
| Kaynak | Hedef |
|---|---|
| C6 3V3 | 3V3 barası |
| C6 GND | GND barası |
| Ekran VCC | 3V3 barası |
| Ekran GND | GND barası |
| Buzzer (−) | GND barası |

**Ekran (SPI + kontrol):**
| Ekran | C6 |
|---|---|
| SCK | IO18 |
| MOSI | IO19 |
| CS | IO9 |
| DC | IO8 |
| RST | IO20 |
| BL | IO21 |

**Joystick / butonlar:**
| Buton | C6 |
|---|---|
| UP | IO6 |
| DOWN | IO11 |
| LEFT | IO23 |
| RIGHT | IO22 |
| A | IO10 |

**Buzzer:**
| Buzzer | C6 |
|---|---|
| (+) | IO1 |

### ⛔ Bu pinlere ASLA tel çekme
- **IO12, IO13** → C6 dahili USB (D−/D+)
- **IO16, IO17** → CH343 USB-seri konsol

---

## 5. İpuçları

- **Grid'e yapıştır (snap to grid) açık olsun** → teller deliklere tam otursun.
- **Güç ve GND için "bara"** çiz (uzun yatay tel), modüller ondan beslensin —
  her modüle ayrı ayrı C6'dan tel çekmek yerine.
- **Renk kodu:** kırmızı=3V3, siyah=GND, sarı/mavi=sinyal. Lehimde işini
  kolaylaştırır.
- **Kaydet** (`.diy` uzantısı). İstersen **File → Export → PNG/PDF** ile resim
  al, telefonuna at, lehim yaparken yanında tut.

---

## 6. Sonraki modüller (çekirdek bitince)

Aynı yönteme RFID/IR/pil ekleyeceğiz. Bağlantıları (RC522 IO18/19/5/7/2,
RDM6300 IO15, IR IO4/IO0, pil bölücü IO3) için:
[`makeshift_flipper.net`](makeshift_flipper.net) ve
[`../WIRING_MAP_RC522.md`](../WIRING_MAP_RC522.md).

Çekirdeği DIYLC'de çizip bana gösterirsen (PNG export), üstünden geçip
"şu tel eksik / şu pin yanlış" diye kontrol ederim.
