# Özellikler, Menü Yapısı, UI Tasarımı ve Kullanım

> Menü ve aksiyon isimleri `main/main.c` içindeki güncel `menu_item_t`
> dizilerinden alındı (2026-09-27). Arayüz Türkçe.

## Kontroller (kullanım)

- **UP / DOWN:** menüde yukarı/aşağı gezinme
- **RIGHT (veya A / CTRL bas):** giriş / seç / onay
- **LEFT:** geri / çık
- Ekran klavyesinde (şifre girişi): kısa LEFT imleç, uzun LEFT geri

## Ana menü (ERDEMFLIP)

Üstte ortalanmış **"ERDEMFLIP"** banner'ı (accent renkli bar). Menü
öğeleri (`s_main_menu_items`):

| # | Öğe | İçerik |
|---|---|---|
| 0 | RFID / NFC | RFID alt menüsü |
| 1 | Kızılötesi | IR alt menüsü |
| 2 | WiFi | WiFi alt menüsü |
| 3 | Bluetooth | BT Tarama |
| 4 | SecLab | Security Lab alt menüsü (pasif araçlar) |
| 5 | Hacking | Hacking alt menüsü (kırmızı uyarı ekranı ile) |
| 6 | Hatalar | Cihaz-üstü hata geçmişi (`diag`) |
| 7 | Hakkında | Sürüm/bilgi ekranı |

## Menüye göre arka plan renkleri

Her kategoriye girince arka plan o bölümün rengine döner (`display.h`'de
`DISPLAY_BG_*`, `main.c`'de `render_menu_themed()` seçer):

| Menü | Renk |
|---|---|
| Ana ekran | Koyu mor |
| RFID/NFC | Koyu mavi |
| Kızılötesi | Koyu kırmızı |
| WiFi | Koyu turkuaz |
| Bluetooth | Koyu indigo/lacivert |
| SecLab | Koyu altın/zeytin |
| Hacking | Siyah (kırmızı yazı) |

**Altyapı:** `display.c` çalışma-zamanı `s_active_bg` değişkeni tutar;
`display_set_background(color)` ile değişir. `display_clear()` ve metin
çizimi bu rengi kullanır. Menü render'dan önce çağrılır.

## WiFi menüsü (en gelişmiş kısım)

`s_wifi_menu_items`:

### 1. WiFi Tara/Bağlan (`action_wifi_scan_test`)
- İstasyon (STA) modunda çevredeki ağları tarar
- **Kaydırılabilir liste** (UP/DOWN), her ağın **sinyal gücü (dBm)** yanında
  - dBm: 0'a yakın = güçlü/yakın (ör. -45 iyi, -85 zayıf)
- **RIGHT/PRESS** ile bir ağ seç → ekran klavyesiyle şifre gir → **bağlan**
- **LEFT** ile çık

### 2. WiFi Durum (`action_wifi_status`)
Bağlıysa: ağ adı, sinyal, kanal, IP, ağ geçidi. Bağlı değilse uyarı.

### 3. WiFi Ağım / AP (`action_wifi_my_network`)  ← QR BURADA
- **RIGHT/A** ile kendi WPA2 SoftAP'ini açar (ad + rastgele şifre gösterir)
- **UP** ile → **Wi-Fi QR kodu** ekrana çıkar:
  - Format: `WIFI:T:WPA;S:<ad>;P:<şifre>;;`
  - Telefonla okutunca telefon **otomatik bu ağa bağlanır** (şifre yazmadan)
  - Beyaz zemin + siyah modüller + kenar boşluğu (okuyucu standardına uygun)
- AP, ekrandan çıkınca da açık kalır; kapatmak için tekrar gir → RIGHT/A

## RFID menüsü (`s_rfid_menu_items`)
13.56MHz (RC522) ve 125kHz (RDM6300) okuma, UID kütüphanesi (kaydet/isim).
**DİKKAT:** RC522 şu an `hardware_profile.h`'de kapalı (GPIO6 çakışması,
bkz. SESSION_01). RDM6300 kod hazır ama 5V+level shifter donanımı gerekli.

## Kızılötesi menüsü (`s_ir_menu_items`)
NEC protokolü gönder / öğren / kütüphane. `ir_driver.c` RMT kullanır.
(Not: RMT `mem_block_symbols` C6'da 48 olmalı — 128 boot'u kilitliyordu,
düzeltildi. Bkz. SESSION_04.)

## Bluetooth
BT Tarama — pasif BLE cihaz taraması (`radio_ble.c`, NimBLE).

## Security Lab (`s_security_lab_menu_items`)
Pasif radyo araçlarının gruplandığı yer: WiFi Survey, BLE Discovery, ve bir
"Lab Safety Guide" uyarı ekranı. Hepsi mevcut pasif fonksiyonları kullanır.

## Hacking (`s_hacking_menu_items`)
Girişte **kırmızı uyarı ekranı** çıkar (`show_hacking_intro()`):
"== HACKING ==, yetkili kullanım, tüm araçlar RX-only, enjeksiyon yok".
İçerik: WiFi Recon (RX), AP Monitor (RX), BLE Recon (RX) — hepsi pasif,
mevcut tarama/monitor fonksiyonlarına kısayol. **Yeni saldırı kodu YOK.**

## UI tasarım detayları (display.c)

- **Panel:** 240×240, RGB565. `DISPLAY_RGB(r,g,b)` makrosu: r 0-31, g 0-63,
  b 0-31.
- **Font:** 8×16 bitmap (`font8x16_basic.c`). Ekran = 30 sütun × 15 satır.
- **Rotasyon:** Cihaz 90° döndürülmüş kullanılıyor. `display.c` init'te
  `esp_lcd_panel_swap_xy(true)` + `esp_lcd_panel_mirror(true,false)` ile
  yatay (landscape) düzeltilir. Yazılar düz görünür.
- **Renk ters çevirme:** `esp_lcd_panel_invert_color(true)` (panel gereği).
- **Çizim API'si:** `display_clear`, `display_draw_text`,
  `display_draw_text_color`, `display_draw_text_px`, `display_fill_rect`,
  `display_flush` (framebuffer'ı panele basar).

## QR üreticisi (ui/qrcode.c) — nasıl çalışır

- Saf C, bağımlılıksız, **statik RAM yükü yok** (çağıran buffer verir).
- QR standardı: **byte modu, ECC seviyesi M, versiyon 1-10 otomatik.**
- Reed-Solomon hata düzeltme (GF(256)) + 8 maskeden en iyisini seçme dahil.
- API:
  - `qr_encode(qr_code_t *out, const char *text)` — genel metin
  - `qr_encode_wifi(out, ssid, password)` — WiFi join string'i üretir+kodlar
- Çizim: `main.c`'deki `show_qr_screen()` framebuffer'a ölçekleyip basar.
- Sınır: versiyon 10'dan (57×57) uzun metin kodlanamaz — WiFi için fazlasıyla
  yeterli.

## Metin girişi (ui/text_entry.c)
Ekran klavyesi (kaydırmalı). Şifre girişinde `mask=true` ile yıldızlar.
`text_entry_init / _render / _handle_button` API'si.
