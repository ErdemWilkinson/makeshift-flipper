#pragma once

#include <stdbool.h>

// Captive-portal GUVENLIK FARKINDALIK ekrani (ESP-IDF, ana firmware icinde).
//
// Sifresiz bir SoftAP acar, gomulu bir DNS sunucusu tum alan adlarini cihaza
// yonlendirir (captive davranis) ve bir HTTP sunucu tek bir yerel sayfa sunar:
//   - kullanici sectigi bir gorsel + "saka" + gonullu bir mesaj kutusu + Gonder
//   - en altta "Her WiFi'ye guvenme" uyarisi
// Gonderilen mesajlar RAM'de tutulur (son birkac tane, kalici degil) ve
// sifre/kullanici-adi (isim/soyisim) korumali bir /mesajlar sayfasindan okunur.
//
// Bu bir kimlik avi araci DEGILDIR: sifre/kimlik alani yoktur, sayfa ne oldugunu
// durustce soyler ve ziyaretciyi uyarir. Tek radyo kisiti: baslatinca WiFi
// monitor / BLE / normal AP ile ayni anda calismaz; durunca radyo serbest kalir.

// Baslatir: sifresiz AP + DNS + HTTP. Basarisizsa false.
bool captive_portal_start(void);

// Durdurur: HTTP + DNS kapanir, AP durur, STA moduna donulur.
void captive_portal_stop(void);

bool captive_portal_is_running(void);

// AP SSID'si (durum ekrani icin). Calismiyorken "".
const char *captive_portal_ssid(void);

// Simdiye kadar gelen toplam mesaj sayisi (kayan tampon dolsa da artar).
unsigned int captive_portal_msg_total(void);
