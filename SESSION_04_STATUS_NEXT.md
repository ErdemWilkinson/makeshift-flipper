# Mevcut Durum, Çözülen Hatalar ve Sıradaki İşler

> En güncel "şu an neredeyiz" dosyası. Yeni session işe **buradan** başlasın.
> Tarih: 2026-09-27.

## ✅ Şu an ÇALIŞAN durum

Cihaz gerçek donanımda çalışıyor ve aşağıdakiler cihazda test edildi:
- ESP32-C6 + Pico-LCD-1.3 bağlı, boot temiz, ana menüde açılıyor
- Ekran, 90° rotasyon, menüler; butonlar (UP/DOWN/RIGHT/A/LEFT)
- Ekran klavyesi: kısa LEFT/RIGHT imleç, "ENT" hücresi onay, uzun LEFT çıkış
- WiFi Tara/Bağlan (yakından uzağa sıralı), WiFi Durum, WiFi Ağım (AP) + QR
- Hacking pasif araçları: WiFi İzleme (zayıf AP kırmızı '!'), Kanal Haritası,
  Çerçeve İstat, Probe Yakala — hepsi yakından uzağa sıralı
- BLE: BT Tara (aktif tarama, isimli+yakın sıralı, cihaz-tipi tahmini,
  iBeacon/Eddystone), BLE Radar (canlı mesafe + kalibre açı, yakından uzağa)

## ⚠️ Flash durumu

Tüm yukarıdaki özellikler 2026-09-27'de COM7'ye **flashlandı ve cihazda
kullanıldı**. En güncel commit: `b8bfeb44`. Yeniden flash:
```powershell
cd C:\mkf_verify; & C:\esp-idf\esp-idf\export.ps1 *>$null; idf.py -p COM7 flash
```
Not: Probe Yakala/tip-tahmini gibi son eklemelerin davranış testi kullanıcıya
kalmış olabilir; kod build+flash edildi.

## 🧩 Bu makinede build/flash (ÖNEMLİ tuzak)

Ana proje yolu Türkçe karakter içeriyor (`Masaüstü`), ESP-IDF bunu bozuyor.
Build/flash **`C:\mkf_verify`** kopyasında yapılır. Kaynak değiştirince o
dosyayı mkf_verify'a KOPYALA, sonra orada build et. (Geçmişte text_entry.c
kopyalanmayı unutuldu, eski kod build edildi — build öncesi tüm değişen
dosyaların senkron olduğunu doğrula.)

## Git geçmişi (bu makinede, `master` dalı — hepsi push'lu)

```
b8bfeb44 Pasif keşif: probe yakala + BLE cihaz tipi + zayıf-AP  <- EN SON
a75f6579 BLE aktif tarama (isimler) + MAC fallback + sıralama + ENTER
2cd36f67 BLE radar + stack-overflow crash + klavye/boot giriş düzeltmeleri
4370ab2d BLE tarama stack-overflow düzeltmesi
b4a47bb1 SESSION belgeleri
dba57ee5 Pasif WiFi kanal haritası + çerçeve istat + BLE beacon
ad6fbabb Wi-Fi QR paylaşımı
```

## 🐞 Çözülen büyük hatalar (tekrar olursa referans)

0. **Stack overflow → reset ("Stack protection fault" / _vfprintf_r):**
   BLE Keşif, BT Tara, WiFi Ağım (QR) gibi ekranlar açılınca cihaz resetliyordu.
   **Kök neden:** main task stack'i sadece 3584 byte; ekranlardaki büyük yerel
   diziler (`c6_bt_device_t devices[32]`, `qr_code_t` ~3.2KB, `networks[16]`,
   `diag_entry_t[24]`) stack'i taşırıyordu. **Fix:** (a) `sdkconfig.defaults`
   `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`, (b) bu büyük dizileri `static`
   yaptım (tek-thread menü döngüsünden çağrıldıkları için güvenli). Yeni büyük
   yerel dizi eklerken bunu hatırla.

0b. **Boot'ta RFID menüsüne atlama:** Yasal uyarıyı geçmek için basılan SAĞ,
   menüye "seç" olarak sızıp RFID'ye giriyordu. **Fix:** `buttons_wait_all_
   released()` — uyarıdan çıkmadan tüm tuşların bırakılmasını bekle + kenar
   durumunu sıfırla.

0c. **Klavyede SOL çalışmıyordu:** İki sorun: (1) `text_entry.c`'nin LEFT-case'li
   sürümü mkf_verify'a kopyalanmamıştı (eski kod build ediliyordu), (2) klavye
   giriş mantığı kırılgandı. **Fix:** klavye girişi ham GPIO seviyesinden
   yeniden yazıldı (`buttons_poll_keyboard`), kısa LEFT=sola, uzun=çıkış.

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
ekleme" dedi. **2026-09-27'de eklenen ve flashlanan** özellikler:

1. ✅ **WiFi çerçeve istatistiği** — `c6_link_monitor_frame_stats()`,
   Hacking → "Çerçeve İstat".
2. ✅ **BLE beacon decoder** — iBeacon + Eddystone, `decode_beacon()`,
   BT listesinde 'i'/'E' etiketi + detay.
3. ✅ **Kanal ısı haritası** — `c6_link_monitor_channel_stats()`,
   Hacking → "Kanal Haritası".
4. ✅ **BLE Radar** — canlı mesafe (yumuşatılmış RSSI) + kalibre açı (bir tur
   dön), yakından uzağa sıralı. Bluetooth → "BLE Radar".
5. ✅ **Aktif BLE tarama** — isimleri getirir (scan-response), MAC fallback,
   isimli+yakın sıralı, cihaz-tipi tahmini (`classify_ble_kind`).
6. ✅ **Probe Yakala** — çevredeki cihazların aradığı SSID'ler,
   `c6_link_monitor_probe_poll()`, Hacking → "Probe Yakala".
7. ✅ **Zayıf AP işareti** — WiFi İzleme'de OPEN/WEP ağlar kırmızı '!'.
8. ✅ **Yakından uzağa sıralama** — tüm listelerde (WiFi tara, WiFi izleme,
   BT tara, BLE radar).
9. ✅ **Ekran klavyesi "ENT"** — onay hücresi netleştirildi, LEFT düzeltildi.

Kalan (henüz yapılmadı):

- **MIFARE sektör haritası** — RC522 gerekir (GPIO6 çakışması, donanım kapalı)
- **IR protokol genişletme** — NEC dışı RC5/RC6/Sony/Samsung
- **Sinyal gücü grafiği** (öneri) — seçili cihazın RSSI zaman grafiği

**Yapılmayacaklar (kullanıcı reddetti):** oyunlar, not defteri, hesap
makinesi, SD kart (donanım yok), IR-barkod (fiziksel imkansız), her türlü
Wi-Fi/BLE saldırı özelliği (deauth/injection/cracking/spoofing/spam).

## Önerilen ilk adım (yeni session için)

1. Bu 5 dosyayı oku.
2. `git log --oneline -6` ve `git status` ile durumu doğrula.
3. Cihaz bağlıysa `idf.py -p COM7 flash` ile son build'i (QR) test et.
4. Kullanıcıya sıradaki işlerden hangisini istediğini sor.
