# OCR Eğitiminin Bu Makinede Neden Yavaşladığı — Araştırma Raporu

## Özet

OCR modelinin eğitimi (`ocr/scripts/train.py`) bu makinede tekrar tekrar aşırı yavaşladı: epoch başına
beklenen ~60-85 saniye yerine, bazı denemelerde tek bir epoch 1.5 saatten uzun sürdü ya da hiç
bitmedi. Süreç her seferinde canlı ve "Responding: True" durumundaydı — kilitlenmedi, sadece
son derece yavaş ilerledi. Kök neden tek bir şey değil, üç faktörün üst üste binmesi:

1. **Bitdefender gerçek zamanlı tarama** — en büyük şüpheli
2. **CPU turbo boost hiç devreye girmiyor gibi görünüyor** (2.3 GHz sabit, üretici spesifikasyonu ~4.7 GHz'e kadar boost destekliyor)
3. **Arka planda rekabet eden ağır uygulamalar** (Chrome, League of Legends istemcisi, Steam, Spotify) CPU zamanını paylaşıyor

## Toplanan kanıtlar

### Donanım — darboğaz değil
- CPU: Intel Core i7-12650H, 10 çekirdek / 16 mantıksal işlemci — bu iş için fazlasıyla yeterli.
- RAM: 32 GB, ~11 GB boş — bellek baskısı yok.
- Disk: Üç SSD, hepsi "Healthy" — disk G/Ç darboğazı değil.

### CPU frekansı — şüpheli
- `CurrentClockSpeed` sürekli `MaxClockSpeed` (2300 MHz) ile aynı çıktı; bu CPU'nun temel (base) hızı.
- i7-12650H normalde tek çekirdek yükünde ~4.7 GHz'e boost yapabilir. Eğitim gibi tek işlemli,
  CPU-yoğun bir görevde bu boost hiç görülmediyse, CPU frekans ölçeklendirmesi (turbo boost)
  aktif değil demektir — bu tek başına 2 kata yakın yavaşlama anlamına gelebilir.

### Güç planı — muhtemel katkıda bulunan
- Aktif güç şeması "Gaming" adında özel/OEM bir profil. Standart Windows "Yüksek performans"
  ya da "Dengeli" değil. Bazı üretici "Gaming" profilleri, GPU'ya öncelik verip CPU'yu sessiz/düşük
  güç modunda tutacak şekilde ayarlanmış olabilir — adının aksine, sürekli CPU yükünde performans
  garantisi vermez.

### Antivirüs — en güçlü şüpheli
- Sistemde **hem Windows Defender hem de Bitdefender Antivirüs** aynı anda kayıtlı görünüyor.
- Bitdefender'a ait 7 ayrı arka plan süreci tespit edildi (`bdagent`, `bdredline` x2, `bdservicehost` x4),
  bunlardan biri 480 MB+ bellek kullanıyor — gerçek zamanlı dosya tarama motoru olması yüksek ihtimal.
- Eğitim scripti (`train.py`) başlangıçta 18.000 küçük PNG dosyasını tek tek diskten okuyup
  belleğe yüklüyor (`tf.io.read_file` + `decode_image`, her görüntü için ayrı bir dosya erişimi).
  Gerçek zamanlı virüs taraması açıksa, bu on binlerce küçük dosya erişiminin her biri tarama
  motoru tarafından senkron olarak kesintiye uğratılabilir — I/O tabanlı görevlerde bilinen,
  yaygın bir yavaşlama nedenidir.
- Windows Defender PowerShell modülü sorgulanamadı (muhtemelen Bitdefender birincil AV olduğu
  için Defender pasif/kısıtlı durumda) — bu da iki antivirüs ürününün aynı sistemde beraber
  var olduğunu, olası çakışan taramalara işaret ediyor.

### Arka plan uygulamaları — küçük katkı
- Testler sırasında Chrome (çok sayıda alt süreç), League of Legends istemcisi, Steam ve Spotify
  aynı anda çalışıyordu. Kullanıcının kendi geri bildirimine göre League tek başına CPU'nun
  ~%30'unu tüketiyor. Bu uygulamalar CPU'yu paylaşınca eğitim süreci daha az zaman dilimi alıyor.
- Ancak tek başına bu yeterli açıklama değil: League kapatıldıktan sonra bile eğitim ilk epoch'u
  hızlı bitirip (56s) sonraki epoch'larda yine saatlerce takılı kaldı — bu, sadece "diğer program
  CPU'yu yiyor" senaryosuyla tam örtüşmüyor ve antivirüs/I-O kaynaklı periyodik yavaşlamayı
  daha olası kılıyor.

## Neden düzensiz (bazen hızlı, bazen çok yavaş)?

Gözlemlenen patern — bir epoch hızlı biter, sonraki epoch saatlerce takılır — şu ihtimallerle uyumlu:
- Bitdefender'ın periyodik arka plan taramaları (zamanlanmış hızlı tarama, tanım güncellemesi
  sonrası yeniden tarama gibi) düzensiz aralıklarla devreye giriyor olabilir.
- Windows'un güç yönetimi, CPU sürekli yükte kaldığında (10-15 dakika sonra) termal/güç
  politikası gereği frekansı kısıyor olabilir (bu "Gaming" planının OEM tarafındaki gizli bir
  davranışı olabilir).
- Diğer uygulamaların (Chrome sekmeleri, League istemcisi arka plan güncellemeleri) kendi
  döngüsel arka plan işleri (otomatik güncelleme kontrolü, senkronizasyon) belirli aralıklarla
  CPU'ya yükleniyor olabilir.

Kesin tek bir neden yerine, bu üç etkenin rastgele çakışması en olası açıklama.

## Önerilen düzeltmeler (önem sırasına göre)

### 1. Bitdefender'da proje klasörünü taramadan hariç tut (en yüksek etki, düşük risk)
Bitdefender ayarlarından **İstisnalar (Exclusions)** bölümüne şu klasörü ekle:
```
C:\Users\erdem\OneDrive\Masaüstü\Makeshift flipper\
```
Bu, gerçek zamanlı taramanın eğitim sırasında okunan/yazılan binlerce küçük dosyayı
kesintiye uğratmasını engeller. Güvenlik riski düşüktür çünkü bu klasör sadece kendi
yazdığın Python kodu ve üretilen görüntü/model dosyalarını içeriyor.

### 2. Güç planını "Yüksek performans" ile değiştirip test et
```
powercfg /setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c
```
(Bu standart "Yüksek performans" planının GUID'idir; sistemde yoksa `powercfg -duplicatescheme
8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c` ile oluşturulabilir.) Eğitim bittikten sonra "Gaming"
planına geri dönülebilir.

### 3. Eğitimi düşük öncelikli değil, normal/yüksek öncelikli çalıştır
Varsayılan olarak arka plan görevleri bazen "Düşük" CPU önceliğinde başlayabiliyor. Eğitim
komutunu Görev Yöneticisi'nden "Yüksek" önceliğe alarak ya da PowerShell'den
`Start-Process ... -Priority High` ile başlatarak diğer uygulamalarla rekabette avantaj sağlanabilir.
(Dikkat: bu, diğer uygulamaları daha çok yavaşlatır — oyun oynarken kullanılmamalı.)

### 4. Eğitimi sadece makine gerçekten boştayken çalıştır
League, Chrome, Steam, Spotify gibi uygulamalar kapalıyken (gece, ya da kullanılmadığı bir
zaman aralığında) başlatmak, hem CPU rekabetini hem de kullanıcının rahatsız olmasını ortadan kaldırır.

### 5. (Opsiyonel, daha büyük değişiklik) Eğitim veri okuma hattını optimize et
`train.py` şu an tüm veri setini tek seferde belleğe yüklüyor (`make_arrays`), bu kısım zaten
disk G/Ç'sini epoch başına tekrar yapmıyor — yani asıl G/Ç yükü sadece başlangıçta. Eğer
antivirüs istisnası sorunu çözmezse, `tf.data.Dataset` tabanlı bir pipeline'a geçmek G/Ç'yi
paralelleştirip antivirüs kesintilerinin etkisini azaltabilir, ama bu şu an önceliksiz —
önce 1 ve 2 numaralı adımlar denenmeli.

## Önerilen sıradaki adım

En yüksek etki/en düşük efor oranına sahip adım **Bitdefender istisnası eklemek**. Bunu
uyguladıktan sonra küçük bir deneme eğitimi (birkaç epoch, örneğin `EPOCHS` değerini geçici
olarak 5'e düşürüp) çalıştırıp epoch süresinin gerçekten ~60-85 saniyeye döndüğünü doğrulamak,
kök nedenin antivirüs olup olmadığını kesin olarak ortaya koyar.
