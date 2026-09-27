# Derleme, Flash ve Monitor

> Bu projede kullanılan gerçek, çalışan komutlar. Ortam: Windows 11,
> ESP-IDF v5.3.1, PowerShell (ana kabuk) + Git Bash.

## ⚠️ En kritik kural: ASCII yol zorunluluğu

Proje asıl konumu Türkçe karakter içeriyor:
`C:\Users\erdem\OneDrive\Masaüstü\Makeshift flipper`

ESP-IDF derleme sistemi **Türkçe karakterli yolları (ü, ı, ş) bozar.** Bu
yüzden derleme/flash **ayrı bir ASCII yol kopyasından** yapılır:

```
C:\mkf_verify\      <- build/flash burada çalışır
```

**İş akışı:** Kaynak dosyaları ana projede düzenle → değişen dosyaları
`C:\mkf_verify\main\...` altına kopyala → orada build/flash et.

### Senkronizasyon (Git Bash ile)
```bash
cd "/c/Users/erdem/OneDrive/Masaüstü/Makeshift flipper"
cp main/main.c            /c/mkf_verify/main/main.c
cp main/ui/qrcode.c       /c/mkf_verify/main/ui/qrcode.c
# ... değiştirdiğin her dosyayı kopyala
```

> **Tuzak:** `mkf_verify` kopyası ana projeden geri kalabilir (özellikle
> başka bir AI/Codex ana projede dosya değiştirdiyse). Build "No such file"
> ya da eski davranış verirse, önce tüm değişen dosyaları senkronize et.
> Kontrol: `diff -q main/<f> /c/mkf_verify/main/<f>`.

## Derleme (PowerShell)

```powershell
cd C:\mkf_verify
& C:\esp-idf\esp-idf\export.ps1 *>$null      # IDF ortamını yükle (sessiz)
idf.py build
```

Başarı işareti: `Project build complete` ve `exited with code 0`.
Uzun sürer (~1-3 dk); arka planda çalıştırmak iyi olur.

## Flash

```powershell
cd C:\mkf_verify
& C:\esp-idf\esp-idf\export.ps1 *>$null
idf.py -p COM7 flash
```

Başarı işareti: `Done` / `Hard resetting via RTS pin`.

### Port bulma
Cihaz USB ile takılıyken:
```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
```
- Çıktı boşsa → cihaz bağlı değil / USB veri kablosu değil / boot takıldı.
- C6-DEV-KIT-NX genelde **iki port** gösterir (COM7 ve COM8). Doğrusu
  genelde COM7; olmazsa COM8 dene.
- USB'yi her çıkarıp taktığında port numarası değişebilir.

### Flash başarısızsa
- `Could not open COMx, port is busy` → başka bir program portu tutuyor
  (monitor açık olabilir) ya da port değişti. Portu yeniden kontrol et.
- Cihaz USB'de hiç görünmüyorsa: USB çıkar-tak; takarken strapping-pin
  butonlarına (DOWN/CTRL) basma. Gerekirse **BOOT'a basılı tutup RESET'e
  bas**, bırak → zorla indirme modu.

## Serial Monitor (boot log okuma)

`idf.py monitor` yerine bu oturumda PowerShell ile ham okuma kullanıldı
(daha kontrollü, port çakışması az):

```powershell
$port = New-Object System.IO.Ports.SerialPort COM7,115200,None,8,one
$port.ReadTimeout = 500
$port.Open()
$port.DtrEnable = $false; $port.RtsEnable = $true   # reset tetikle
Start-Sleep -Milliseconds 100
$port.RtsEnable = $false
$deadline = (Get-Date).AddSeconds(6)
$buf = ""
while ((Get-Date) -lt $deadline) {
  try { $buf += $port.ReadExisting() } catch {}
  Start-Sleep -Milliseconds 50
}
$port.Close()
$buf -split "`n" | Where-Object { $_ -match 'display|Guru|panic|rc522|c6_link' }
```

Başarılı boot'ta görülmesi gerekenler:
```
ST7789 LCD initialized (SCK=18 MOSI=19 CS=9 DC=8 RST=20)
c6_link: Wi-Fi STA ready (on-chip radio)
radio_ble: BLE scan stack initialized
main_task: Calling app_main()
```

## Boyut / kaynak raporu

```powershell
cd C:\mkf_verify; & C:\esp-idf\esp-idf\export.ps1 *>$null; idf.py size
```

Son ölçüm (2026-09-27, QR eklendikten sonra):
- **RAM (DIRAM):** ~%67 dolu, ~148 KB boş. En büyük tek yük: 240×240×2 =
  **112 KB framebuffer** (`display.c`'de statik `s_framebuf`).
- **Flash (app):** ~1.3 MB / 3 MB partition → ~1.7 MB boş.
- **Sonuç:** Flash bol, RAM orta-dar. Yeni özellik eklerken **büyük statik
  buffer'dan kaçın** (ikinci framebuffer, dev tablo). Birkaç KB'lik geçici
  RAM sorun değil.

## Panic adresi çözme (debug)

Guru Meditation / watchdog panic'te `MEPC` adresini fonksiyona çevir:
```powershell
cd C:\mkf_verify; & C:\esp-idf\esp-idf\export.ps1 *>$null
riscv32-esp-elf-addr2line -e build\makeshift_flipper.elf -f -C 0x42056f48
```
(RDM6300/UART1 hatası tam böyle çözüldü — bkz. SESSION_04.)
