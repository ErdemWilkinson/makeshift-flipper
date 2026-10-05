# Lehim — Adım 1: Çalışan Çekirdek

Sadece **C6 + Ekran + Joystick + Buzzer**. Bunlar zaten çalışıyor; amaç
breadboard yerine pertinaksa **kalıcı** lehimlemek. RFID / IR / pil YOK
(sonraki adımlar). Breadboard gerekmez, KiCad gerekmez, ekstra parça gerekmez.

> **Toplam bağlantı sayısı: 15 tel.** Az, korkma. Teker teker gideceğiz.

---

## Ne kullanacaksın
- Delikli pertinaks
- Dişi pin header şeritleri (C6 ve ekran bunlara TAKILACAK, lehimlenmeyecek)
- Lehim setinden çıkan kablo (uçlarını sıyırıp lehimleyeceğin tel)
- Havya, lehim, yan keski, multimetre

---

## Bağlantı listesi (çekirdek — 15 tel)

### Güç (3 tel bara + dağıtım)
Önce pertinaksın bir kenarına iki kısa "bara" (ortak hat) yap: **3V3** ve **GND**.
Ekran ve buzzer güçlerini buradan alacak.

| Kaynak | → Hedef |
|---|---|
| C6 **3V3** | 3V3 bara |
| C6 **GND** | GND bara |

### Ekran (LCD) — 6 sinyal + 2 güç
| LCD pini | → C6 pini |
|---|---|
| SCK | IO18 |
| MOSI | IO19 |
| CS | IO9 |
| DC | IO8 |
| RST | IO20 |
| BL | IO21 |
| VCC | 3V3 bara |
| GND | GND bara |

### Joystick / butonlar (5 sinyal) — hepsi LCD modülünün üstünde
| Buton | → C6 pini |
|---|---|
| UP | IO6 |
| DOWN | IO11 |
| LEFT | IO23 |
| RIGHT | IO22 |
| A | IO10 |

> Butonların ayrı GND'si LCD modülünün GND'siyle ortak — ekranın GND'sini
> bağladıysan butonlar da topraklıdır.

### Buzzer (aktif) — 2 tel
| Buzzer | → |
|---|---|
| (+) | IO1 |
| (−) | GND bara |

---

## ⛔ ASLA bağlama
| Pin | Neden |
|---|---|
| IO12, IO13 | C6'nın dahili USB'si — bağlarsan USB portu kapanır |
| IO16, IO17 | CH343 USB-seri (flash/log) hattı |

---

## Lehim sırası (test ederek)

**1) Header soketlerini lehimle.**
C6 ve ekran için dişi header'ları pertinaksa lehimle. Modülleri henüz TAKMA.
Sadece soketler lehimli olacak; modüller sonra takılıp çıkarılabilir.

**2) Güç baralarını çek.**
3V3 ve GND baralarını lehimle. C6 soketinin 3V3/GND bacaklarını baralara bağla.

**3) Multimetre kontrolü (güç VERMEDEN).**
Süreklilik modunda: **3V3 bara ↔ GND bara arası ses çıkmamalı** (kısa devre yok).
Çıkıyorsa lehim köprüsü var, bul ve temizle. Temizse devam.

**4) C6'yı tak, USB ver.**
Sadece C6. Seri logda boot görünmeli / cihaz açılmalı. Ekran daha yok, normal.

**5) Ekran tellerini lehimle, ekranı tak.**
Yukarıdaki 8 bağlantıyı yap. USB ver → menü ekranda görünmeli.

**6) Joystick tellerini lehimle.**
5 buton telini bağla. Menüde UP/DOWN/LEFT/RIGHT/A ile gezebiliyorsan tamam.

**7) Buzzer'ı lehimle.**
(+)→IO1, (−)→GND. Bir menüye girince/çıkınca "biiip" ötmeli.

Bittiğinde: elinde **kablosuz, sağlam, çalışan** bir cihaz var. 🎉

---

## Yeni başlayan lehim ipuçları (kısa)
- Havyayı ısıt, ucunu süngerle temizle, uca ince lehim sür (tinning).
- Isıt (hem ped hem bacak ~2 sn) → lehimi **ekleme noktasına** değdir (havyaya değil) → akınca çek.
- İyi lehim: **parlak, küçük koni.** Mat/top gibi = soğuk lehim, tekrar ısıt.
- İki komşu ped birleştiyse (köprü) = kısa devre. Isıtıp lehim fitiliyle al.
- Her modülü lehimledikten SONRA multimetreyle o modülün VCC↔GND arası kısa var mı bak.

---

## Bir şey çalışmazsa
1. O modülün **GND** teli bağlı mı? (en sık unutulan)
2. **VCC** doğru baraya mı? (çekirdekte hepsi 3V3)
3. Sinyal teli doğru C6 pinine mi gidiyor? Multimetre ile pinden pine süreklilik.
4. Lehim parlak mı, yoksa soğuk lehim/köprü mü?

Çekirdek çalışınca sıradaki adım: RFID (RC522) → bunun için ayrı bir kısa
liste veririm. Şimdilik buna odaklan.
