# Mevcut Durum, Çözülen Hatalar ve Sıradaki İşler

> En güncel "şu an neredeyiz" dosyası. Yeni session işe **buradan** başlasın.
> Tarih: 2026-09-27.

## ✅ Şu an ÇALIŞAN durum

Cihaz gerçek donanımda çalışıyor:
- ESP32-C6 + Pico-LCD-1.3 bağlı, boot temiz
- Ekran çalışıyor, 90° rotasyon düzgün, menüler görünüyor
- Butonlar çalışıyor (UP/DOWN gezinme, RIGHT/A seç, LEFT geri)
- Menü renkleri, ERDEMFLIP banner, Hacking kırmızı ekran hepsi aktif
- WiFi tara/bağlan (dBm + kaydırma), WiFi Durum, WiFi Ağım (AP) çalışıyor
- WiFi QR paylaşımı flashlandı (göz doğrulaması bekliyor)
- Pasif Hacking araçları flashlandı (göz doğrulaması bekliyor): WiFi İzleme,
  Kanal Haritası, Çerçeve İstat, BLE Keşif (iBeacon/Eddystone etiketli)

## ⚠️ Flash durumu

**Son commit (`dba57ee`, pasif WiFi/BLE özellikleri) 2026-09-27'de COM7'ye
FLASHLANDI** (`Hash of data verified`, `Done`). Bir önceki WiFi QR commit'i
de bu flash'a dahil. Flash yazma/doğrulama başarılı — ancak ekran davranışı
(kanal haritası çubukları, çerçeve istat sayaçları, BLE beacon etiketi)
fiziksel panelde **kullanıcı tarafından göz doğrulaması bekliyor**; kod
incelemesi + build + başarılı flash bunu kanıtlamaz.

Yeniden flash gerekirse:
```powershell
cd C:\mkf_verify; & C:\esp-idf\esp-idf\export.ps1 *>$null; idf.py -p COM7 flash
```
Test edilecekler: WiFi Ağım (AP) → UP → QR; Hacking → Kanal Haritası (RX);
Hacking → Çerçeve İstat (RX); BT Tara → beacon 'i'/'E' etiketi + detay.

## Git geçmişi (bu makinede, `master` dalı)

```
dba57ee  Pasif WiFi kanal haritası + çerçeve istat + BLE beacon  <- EN SON (flashlandı)
ad6fbab  Wi-Fi QR sharing + DIAG temizliği   (flashlandı)
0e3d34c  Hacking menü arka planı siyah
c62db1f  C6 doğrudan kontroller + Türkçe menüler   (Codex)
cebc020  Donanım bring-up: butonlar, rotasyon, renkler
830f534  RDM6300/UART1 watchdog düzeltmesi
138f0d2  İlk C6 donanım bring-up: RMT + GPIO16
```

## 🐞 Çözülen büyük hatalar (tekrar olursa referans)

1. **RMT boot-abort:** `ir_driver.c`'de `mem_block_symbols=128` C6'da
   `rmt_new_rx_channel`'ı sonsuz `ESP_ERR_NOT_FOUND` yapıyordu → boot'ta
   abort. **Fix:** RX ve TX için 48'e düşürüldü (C6'da 1 bellek bloğu = 48).

2. **RDM6300/UART1 watchdog hang:** `rdm6300_init` → `uart_ll_update`
   sonsuz döngü. **Kök neden:** ESP-IDF v5.3.1 ikincil HP UART'ın *fonksiyon
   clock*'unu (sclk) açmıyor, sadece bus clock. **Fix:** `rdm6300.c`'de
   `uart_driver_install`'dan önce UART1 sclk manuel açılıyor
   (`HP_UART_SRC_CLK_ATOMIC` + `uart_ll_sclk_enable`). GPIO değişikliği
   çözüm DEĞİLDİ; clock'tu.

3. **Butonlar çalışmıyordu:** Kod TCA9554 I2C expander arıyordu ama bu
   ekranda o çip YOK (butonlar doğrudan GPIO). Boot'ta sürekli I2C NACK
   spam'i. **Fix:** `buttons.c` doğrudan-GPIO okumaya çevrildi.

4. **Ekran boş kalması (strapping pin):** DOWN=IO0, PRESS=IO5 boot pinleri;
   basılı kalınca cihaz yanlış moda girip ekranı boş bırakıyordu. **Fix:**
   buton pinleri boot-güvenli olanlara taşındı (DOWN→IO11, PRESS→IO3).

## 🔻 Bilinen kısıtlar / bilinçli kapalılar

- **RC522 kapalı** (`hardware_profile.h`, GPIO6 çakışması — joystick UP ile).
- **RDM6300** kod hazır ama 5V + level shifter donanımı bağlı değil.
- **B butonu devre dışı** (pini DOWN'a verildi).
- **CTRL/PRESS (IO3)** kablosunun fiziksel varlığı doğrulanmalı.

## 📋 KNOWN_ISSUES.md hakkında

`KNOWN_ISSUES.md` çok uzun ve "Round 1..29" bulguları içerir. **Çoğu
tarihseldir** (eski P4/iki-çip mimarisi, ya da statik inceleme). Round 29'da
başka bir AI'ın eklediği 4 bulgu (NVS erase, WiFi event race, 32-byte SSID
truncation, monitor STA drop) **incelendi**: hepsi ya kabul edilebilir risk
ya kasıtlı tasarım ya da düşük-etkili edge-case. Acil düzeltme gerektiren
gerçek bug bulunamadı. Yeni bir bug ararken önce gerçek kodu doğrula.

## 🤝 ÇOK-AI UYARISI (çok önemli)

Bu projede **birden fazla AI aynı anda çalıştı** (bu Claude + Codex/Continue
eklentisi). Sonuçları:
- Bazı dosyalar (özellikle `buttons.c`, `menu.c`, `c6_link.c`) beklenmedik
  şekilde değişmiş olabilir — başka AI düzenlemiş olabilir.
- Kullanıcı bir noktada "Codex butonu düzeltti, öyle kalsın" dedi.
- **Kural:** Değişiklik yapmadan önce `git status` ve `git diff` ile gerçek
  durumu gör. Kör commit/overwrite yapma. Başka AI'ın uydurduğu
  "doğrulama raporlarına" güvenme (geçmişte Sub-GHz, "hardware abstraction/"
  klasörü gibi var-olmayan şeyler uydurulmuştu).

## 🧹 Repo temizliği gereken çöp (commit edilmemeli)

`.continue/`, `.continueignore`, `debug.log`, `build-c6-current/`, kökteki
boş `ui/` klasörü (içinde işlevsiz `hacking_menu.c` iskeleti) — bunlar
firmware'a ait değil. `.gitignore`'a eklenebilir.

## ➡️ SIRADAKI İŞLER

Kullanıcı "ekleyebileceklerini ekle, boş şeyler (oyun/not/hesaplayıcı)
ekleme" dedi. Aşağıdaki listenin ilk üçü **2026-09-27'de yapıldı, derlendi
(uyarısız) ve flashlandı** (commit `dba57ee`):

1. ✅ **WiFi çerçeve istatistiği** — promiscuous modda mgmt/beacon/probe/data/
   ctrl sayımı + istatistik ekranı. `c6_link_monitor_frame_stats()`,
   Hacking → "Çerçeve İstat (RX)".
2. ✅ **BLE beacon decoder** — iBeacon (manufacturer data) + Eddystone
   (service data) çözme. `radio_ble.c` decode_beacon(), BT listesinde
   'i'/'E' etiketi + detay ekranı.
3. ✅ **Kanal ısı haritası** — 1-13 kanal doluluğu (AP sayısı + en iyi RSSI)
   çubuk grafik. `c6_link_monitor_channel_stats()`, Hacking → "Kanal
   Haritası (RX)".

Kalan (henüz yapılmadı):

4. **MIFARE sektör haritası** — hangi sektörler okunabilir/kilitli
   (RC522 gerekir — önce GPIO6 çakışması çözülmeli, donanım şu an kapalı)
5. **IR protokol genişletme** — NEC dışı RC5/RC6/Sony/Samsung

**Yapılmayacaklar (kullanıcı reddetti):** oyunlar, not defteri, hesap
makinesi, SD kart (donanım yok), IR-barkod (fiziksel imkansız), her türlü
Wi-Fi/BLE saldırı özelliği (deauth/injection/cracking).

## Önerilen ilk adım (yeni session için)

1. Bu 5 dosyayı oku.
2. `git log --oneline -6` ve `git status` ile durumu doğrula.
3. Cihaz bağlıysa `idf.py -p COM7 flash` ile son build'i (QR) test et.
4. Kullanıcıya sıradaki işlerden hangisini istediğini sor.
