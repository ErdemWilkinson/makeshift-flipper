# Makeshift Flipper — 3D model

Cihazın gerçek parça listesine (BOM) göre interaktif 3D görünümü.

## Dosyalar

- **`viewer.html`** — tarayıcıda açılan interaktif 3D sahne. Three.js (CDN).
- **`makeshift_flipper.glb`** — Blender/başka araçlara aktarım için dışa aktarılmış model.

## Nasıl açılır

**viewer.html:** çift tıkla. Çalışmazsa (CDN/tarayıcı kısıtı) bu klasörde:
```
python -m http.server 8731
```
sonra `http://localhost:8731/viewer.html`.

**Blender:** `makeshift_flipper.glb` dosyasını Blender penceresine sürükle-bırak
(veya File → Import → glTF 2.0). viewer.html'deki "(indir: .glb)" kutusunu
işaretleyerek her zaman güncel bir .glb yeniden üretebilirsin.

## Kontroller

- **Montajlı / Patlatılmış / Kasasız** — cihaz bütün, parçalar açık, veya kasa gizli.
- **Patlatma kaydırıcısı** — 0–100 arası sürekli açılım.
- Fare: sürükle döndür · tekerlek yakınlaştır · sağ tık kaydır.
- Bir parçanın/kablonun üstüne gel → ne olduğu ve hangi GPIO'ya gittiği.
- Sağdaki listede bir satırın üstüne gel → o parça sahnede parlar.

## Modellenen gerçek parçalar

ESP32-C6-DEV-KIT (WROOM-1), Pico-LCD-1.3 (ekranda gerçek menü), RC522, RDM6300 +
125 kHz bobin, VS1838B IR alıcı, IR LED, 2×2N2222, 6×14 mm titreşim motoru,
TP4056 Type-C, MT3608 boost, ON/OFF anahtar, level shifter, LiPo 3.7V, bölücü
dirençleri + kondansatörler + PPTC. Kaynak: `docs/planning/ERDEM_SATIN_ALINACAKLAR.md`.

## Doğruluk notları

- **Ölçüler** üretici verisinden: LCD 52×26.5, RC522 60×40, RDM6300 PCB 38.5×19
  + bobin 46×33, TP4056 Type-C 28×17, MT3608 37×17×14, motor 6×14. ESP32 kartı
  (51.8×25.4, Espressif DevKitC-1 çizimi; Waveshare ölçüsü doğrulanmadı) ve bazı küçük
  parçalar yaklaşık. IR verici KY-005 modülü olarak değil, çıplak LED olarak çizili.
- **Kablolar ve pinler şematik** — hangi modülün hangi GPIO'ya bağlandığını
  gösterir (`WIRING_MAP_RC522.md` ile aynı), kart üzerindeki fiziksel header
  sırasını değil.
- **Kasa ve iç yerleşim kavramsal** — gerçek bir 3D baskı kasası için kumpas
  ölçüleriyle ayrıca tasarlanmalı. Montajlı görünüm "ne nereye oturur"
  fikrini verir, birebir montaj kılavuzu değildir.
