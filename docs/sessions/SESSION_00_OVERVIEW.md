# Makeshift Flipper (ERDEMFLIP) — Proje Genel Bakış

> Bu 5 dosyalık `SESSION_*` seti, projeyi hiç görmemiş başka bir AI session'ının
> (veya insanın) sıfırdan tam kavraması için yazıldı. Hepsi 2026-09-27
> itibarıyla gerçek koda dayanır, tahmin değil.
>
> - `SESSION_00_OVERVIEW.md`  — bu dosya: proje nedir, mimari, felsefe
> - `SESSION_01_HARDWARE.md`  — donanım, pinler, kablolama
> - `SESSION_02_BUILD_FLASH.md` — derleme/flash/monitor komutları
> - `SESSION_03_FEATURES_UI.md` — menüler, özellikler, kullanım, tasarım
> - `SESSION_04_STATUS_NEXT.md` — mevcut durum, bilinen sorunlar, yapılacaklar

---

## Bir cümleyle

Makeshift Flipper (cihaz üstündeki adıyla **ERDEMFLIP**), tek bir
**ESP32-C6** üzerinde çalışan, taşınabilir bir güvenlik-araştırma / hobi
cihazıdır: RFID/NFC okuma, kızılötesi (IR) gönder/öğren, Wi-Fi ve BLE
tarama/izleme, hepsi 1.3" renkli bir LCD ve bir joystick+buton takımıyla
kullanılır.

## Ne DEĞİL

- İnternete bağlı bir servis değil. Tamamen **kapalı, cihaz-üstü** çalışır.
- Yapay zekâ içermez (eski sürümlerde bir "Ask AI" özelliği vardı, kaldırıldı).
- Saldırı aracı değil: Wi-Fi/BLE tarafı **pasif** (dinleme/tarama). Deauth,
  paket enjeksiyonu, handshake yakalama, şifre kırma **bilinçli olarak
  eklenmedi** ve eklenmeyecek. Kullanıcı bunu birçok kez netleştirdi.

## Mimari (önemli tarihçe)

Bu proje **iki kez** büyük değişim geçirdi. Kod tabanında ikisinin de izleri
(yorumlar, arşiv) var, bu yüzden karışıklığı önlemek için:

1. **Eskiden iki çipti:** Bir ESP32-P4 (UI/beyin) + bir ESP32-C6 (radyo),
   aralarında UART protokolüyle konuşurlardı. **Bu mimari terk edildi.**
2. **Şimdi tek çip:** Her şey tek bir **ESP32-C6** üzerinde. UI de radyo da
   aynı çipte. UART tel-protokolü yok; eski `c6_link_*` API'si artık
   doğrudan `esp_wifi_*` / NimBLE çağırıyor (aynı isimler korundu ki UI kodu
   değişmesin).

> **Uyarı (başka session'lar için):** Kodda `c6-firmware/`, "P4", "UART
> protokolü", `wifi_setup_ap.c` gibi geçen yorumlar/dosyalar **tarihseldir**.
> `main/` içindeki güncel koda bakın; `archive/` klasörü eski P4 kodudur ve
> derlemeye girmez. `KNOWN_ISSUES.md` içindeki "Round 1..27" bulguları da
> büyük ölçüde tarihseldir.

## Teknoloji yığını

- **MCU:** ESP32-C6 (RISC-V, tek çekirdek, Wi-Fi 6 + BLE 5 + 802.15.4)
- **Kart:** Waveshare **ESP32-C6-DEV-KIT-NX**
- **Ekran:** Waveshare **Pico-LCD-1.3** (ST7789, 240×240 SPI, joystick+A/B/X/Y)
- **Framework:** ESP-IDF **v5.3.1** + FreeRTOS, dil **C**
- **Diğer donanım (envanterde, kısmen bağlı):** RC522 (13.56MHz RFID),
  RDM6300 (125kHz RFID), IR alıcı/verici, titreşim motoru, level shifter

## Kod düzeni (`main/` altında)

| Klasör | İçerik |
|---|---|
| `main.c` | Uygulama girişi (`app_main`), tüm menü aksiyonları, ana döngü |
| `ui/` | `display.c` (ST7789 sürücü + çizim), `menu.c` (menü sistemi), `text_entry.c` (ekran klavyesi), `qrcode.c` (QR üretici), `font8x16_basic.c` |
| `input/` | `buttons.c` (doğrudan GPIO buton okuma) |
| `rfid/` | `rc522.c` (13.56MHz), `rdm6300.c` (125kHz), `rfid_library.c` (UID kaydetme) |
| `ir/` | `ir_driver.c` (RMT), `ir_nec.c` (NEC kodek), `ir_library.c`, `ir_direction.c` |
| `net/` | `c6_link.c` (Wi-Fi: tara/bağlan/monitor/AP), `radio_ble.c` (BLE tarama) |
| `feedback/` | `vibration.c` |
| `diag/` | `diag.c` (cihaz-üstü hata geçmişi, NVS'de saklanır) |
| `hardware_profile.h` | Hangi çevre biriminin bağlı olduğunu tutan derleme-zamanı profili |

## Tasarım felsefesi (kullanıcının tercihleri)

- **Boş/gösteriş özellik yok.** Kullanıcı açıkça istedi: oyun, not defteri,
  hesap makinesi gibi "doldurmalık" şeyler EKLENMEYECEK. Sadece cihazın asıl
  amacına (güvenlik/donanım araçları) hizmet eden özellikler.
- **Pasif kalır.** Wi-Fi/BLE dinler, saldırmaz.
- **Türkçe arayüz.** Menüler ve ekranlar Türkçe (örn. "Kızılötesi", "Hatalar",
  "WiFi Tara/Bağlan").
- **Cihaz-üstü, kapalı sistem.** Dış bağımlılık yok.

## Bu belgeler nasıl kullanılır

Yeni bir session açtığında bu 5 dosyayı sırayla okut. `SESSION_04` en
güncel "şu an neredeyiz / sırada ne var" durumunu tutar — işe oradan başla.
