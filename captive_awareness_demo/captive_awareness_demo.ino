/*
 * Makeshift Flipper — Captive Portal FARKINDALIK DEMOSU (ESP32-C6, Arduino)
 * ------------------------------------------------------------------------
 * NE YAPAR:
 *   - Sifresiz bir SoftAP acar, DHCP ile IP dagitir.
 *   - Gomulu DNS sunucusu TUM alan adlarini cihazin IP'sine cevirir (DNS
 *     hijack) -> baglanan herkes tek yerel sayfaya duser (captive portal).
 *   - Sayfa: "SEN / BEDAVA WIFI" kedi gorseli + gonullu bir MESAJ kutusu +
 *     "Gonder" + en altta "Her WiFi'ye guvenme" uyarisi. Artifact temasi
 *     (koyu zemin, amber/teal vurgu).
 *   - Gonderilen mesaj SADECE USB seri log'a yazilir (RAM, son birkac tane).
 *
 * NE YAPMAZ (bilerek):
 *   - Sifre / e-posta / kimlik alani YOK. Kimlik toplamaz.
 *   - Gercek bir site/marka taklit etmez; sayfa ne oldugunu durustce soyler
 *     ve ziyaretciyi "acik WiFi'ye guvenme" diye UYARIR.
 *
 * Bu bir GUVENLIK FARKINDALIK ekranidir; kimlik avi araci DEGILDIR.
 *
 * KURULUM (Arduino IDE):
 *   - Kart: "ESP32C6 Dev Module" (esp32 board paketi 3.x)
 *   - Ek kutuphane yok (DNSServer + WebServer paketle gelir).
 *   - catimg.h ayni klasorde olmali (kedi gorseli).
 */

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include "catimg.h"   // static const char CAT_IMG_B64[]

// ---- Ayarlar ----
static const char *AP_SSID = "Bedava-WiFi";   // istedigin adi ver
static const byte  DNS_PORT = 53;
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);

// --- Moderator sayfasi (/mesajlar) girisi ---
// /mesajlar'i ACIP goren tek kisi sen olacaksin: tarayici kullanici adi (isim)
// + sifre (soyisim) sorar (HTTP Basic Auth).
// Kullanici adi = isim, sifre = soyisim.
static const char *MOD_USER = "erdem";
static const char *MOD_PASS = "wilkinson";

DNSServer dnsServer;
WebServer webServer(80);

// Son gelen mesajlar RAM'de (kalici degil, cihaz kapaninca silinir).
static const int MSG_KEEP = 20;
String  msgs[MSG_KEEP];
int     msgCount = 0;
unsigned long msgTotal = 0;   // toplam kac mesaj geldi (kayan tampon dolsa bile)

// ---- Artifact temasi (renk tonlarim) ----
// --bg koyu, --card biraz acik, --accent amber, --ok teal, --dim gri, --warn kirmizi
static const char *CSS = R"CSS(
:root{
  --bg:#0f1115; --card:#171a21; --line:#242833;
  --text:#e7e9ee; --dim:#9aa1ad;
  --accent:#f5b301; --ok:#2dd4a7; --warn:#ff6b6b;
}
*{box-sizing:border-box}
body{margin:0;font-family:ui-sans-serif,system-ui,-apple-system,Segoe UI,Roboto,Arial,sans-serif;
  background:radial-gradient(1200px 600px at 50% -10%,#1a1f2b 0%,var(--bg) 60%);
  color:var(--text);min-height:100vh;display:flex;flex-direction:column;align-items:center;
  padding:20px 16px 40px}
.wrap{width:100%;max-width:440px;margin:0 auto;text-align:center}
.hero{position:relative;border-radius:18px;overflow:hidden;border:1px solid var(--line);
  box-shadow:0 12px 40px rgba(0,0,0,.45);margin-bottom:18px}
.hero img{display:block;width:100%;height:auto}
.badge{position:absolute;top:12px;left:12px;background:rgba(15,17,21,.72);
  border:1px solid var(--line);color:var(--accent);font-size:12px;font-weight:700;
  letter-spacing:.5px;padding:6px 10px;border-radius:999px;backdrop-filter:blur(6px)}
.card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:18px;text-align:center}
h1{font-size:20px;margin:0 0 4px;letter-spacing:.2px}
h1 .dot{color:var(--accent)}
p.note{font-size:13px;color:var(--dim);opacity:.6;margin:0 0 14px;line-height:1.5}
textarea{width:100%;min-height:96px;border-radius:12px;border:1px solid var(--line);
  background:#0d0f14;color:var(--text);padding:12px;font-size:15px;resize:vertical;outline:none;
  text-align:left}
textarea:focus{border-color:var(--accent)}
button{margin-top:12px;width:100%;padding:13px;border:0;border-radius:12px;cursor:pointer;
  background:linear-gradient(180deg,var(--ok),#1fb692);color:#04140d;font-weight:800;font-size:16px}
button:active{transform:translateY(1px)}
.warn{margin-top:18px;display:flex;gap:10px;align-items:center;justify-content:center;
  padding:12px 14px;border-radius:12px;background:rgba(255,107,107,.10);
  border:1px solid rgba(255,107,107,.35);color:var(--warn);font-weight:700;font-size:14px}
.foot{margin-top:14px;text-align:center;color:var(--dim);font-size:12px}
a{color:var(--ok);text-decoration:none}
)CSS";

// ---- Ana sayfa ----
static String buildPage() {
  String h;
  h.reserve(4096);
  h += F("<!doctype html><html lang='tr'><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>Bedava WiFi</title><style>");
  h += CSS;
  h += F("</style></head><body><div class='wrap'>");

  // Kedi gorseli (hero)
  h += F("<div class='hero'><span class='badge'>BILGILENDIRME</span>"
         "<img alt='SEN / BEDAVA WIFI' src='data:image/jpeg;base64,");
  h += CAT_IMG_B64;
  h += F("'></div>");

  // "saka" -- fotonun hemen altinda
  h += F("<p class='note'>&quot;saka&quot;</p>");

  // Mesaj karti
  h += F("<div class='card'>"
         "<h1>Bir mesaj birak<span class='dot'>.</span></h1>"
         "<form method='POST' action='/msg'>"
         "<textarea name='m' maxlength='240' placeholder='Buraya yaz...'></textarea>"
         "<button type='submit'>Gonder</button>"
         "</form></div>");

  // Uyari
  h += F("<div class='warn'>&#9888; Her WiFi'ye guvenme.</div>");

  h += F("</div></body></html>");
  return h;
}

static String thanksPage() {
  String h;
  h.reserve(1024);
  h += F("<!doctype html><html lang='tr'><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>Tesekkurler</title><style>");
  h += CSS;
  h += F("</style></head><body><div class='wrap'><div class='card' style='text-align:center'>"
         "<h1>Tesekkurler<span class='dot'>.</span></h1>"
         "<p class='note'>Mesajin alindi.</p>"
         "<p><a href='/'>&larr; Geri don</a></p></div>"
         "<div class='warn'>&#9888; Her WiFi'ye guvenme.</div>"
         "</div></body></html>");
  return h;
}

// ---- Handler'lar ----
static void handleRoot() {
  webServer.send(200, "text/html; charset=utf-8", buildPage());
}

static void handleMsg() {
  if (webServer.hasArg("m")) {
    String m = webServer.arg("m");
    m.trim();
    if (m.length() > 0) {
      if (msgCount < MSG_KEEP) {
        msgs[msgCount++] = m;
      } else {
        for (int i = 1; i < MSG_KEEP; i++) msgs[i - 1] = msgs[i];
        msgs[MSG_KEEP - 1] = m;
      }
      msgTotal++;
      Serial.print("[MESAJ] ");
      Serial.println(m);   // moderator (sen) seri monitorden de okuyabilir
    }
  }
  webServer.send(200, "text/html; charset=utf-8", thanksPage());
}

// Mesaji sayfaya basmadan once HTML'i kacir (mesaj sayfayi bozmasin).
static String htmlEscape(const String &in) {
  String o; o.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '&': o += F("&amp;");  break;
      case '<': o += F("&lt;");   break;
      case '>': o += F("&gt;");   break;
      case '"': o += F("&quot;"); break;
      default:  o += c;
    }
  }
  return o;
}

// Moderator sayfasi: cihazin IP'sinde /mesajlar -> gelen tum mesajlar (RAM).
// Kullanici adi + sifre ile korunur; sadece dogru giris yapan gorur.
static void handleMesajlar() {
  // HTTP Basic Auth: yanlis/eksik giriste tarayici giris kutusu acar.
  if (!webServer.authenticate(MOD_USER, MOD_PASS)) {
    // Kullanici adi = isim, sifre = soyisim.
    return webServer.requestAuthentication(
      BASIC_AUTH, "Moderator", "Isim (kullanici) ve soyisim (sifre) ile gir");
  }

  String h;
  h.reserve(2048);
  h += F("<!doctype html><html lang='tr'><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<meta http-equiv='refresh' content='5'>"  // 5 sn'de bir kendini yenile
         "<title>Mesajlar</title><style>");
  h += CSS;
  h += F("</style></head><body><div class='wrap'>"
         "<div class='card'><h1>Mesajlar<span class='dot'>.</span></h1>"
         "<p class='note' style='opacity:1'>");
  h += F("Toplam: ");
  h += String(msgTotal);
  h += F(" &middot; saklanan son ");
  h += String(MSG_KEEP);
  h += F("</p>");

  if (msgCount == 0) {
    h += F("<p class='note' style='opacity:1'>Henuz mesaj yok.</p>");
  } else {
    // En yeni ustte
    for (int i = msgCount - 1; i >= 0; i--) {
      h += F("<div style='padding:10px 12px;margin:8px 0;border-radius:10px;"
             "background:#0d0f14;border:1px solid var(--line);text-align:left'>");
      h += htmlEscape(msgs[i]);
      h += F("</div>");
    }
  }
  h += F("</div><div class='foot'>5 sn'de bir yenilenir</div>"
         "</div></body></html>");
  webServer.send(200, "text/html; charset=utf-8", h);
}

// Captive portal: bilinmeyen her istegi ana sayfaya yonlendir.
static void handleNotFound() {
  webServer.sendHeader("Location", String("http://") + AP_IP.toString(), true);
  webServer.send(302, "text/plain", "");
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("Captive FARKINDALIK demosu baslatiliyor...");

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
  WiFi.softAP(AP_SSID);                 // sifresiz (acik) AP
  Serial.print("AP SSID: "); Serial.println(AP_SSID);
  Serial.print("AP IP:   "); Serial.println(WiFi.softAPIP());

  dnsServer.start(DNS_PORT, "*", AP_IP);   // her alan adi -> bu IP

  webServer.on("/", handleRoot);
  webServer.on("/msg", HTTP_POST, handleMsg);
  webServer.on("/mesajlar", handleMesajlar);  // moderator sayfasi (gizli link)
  webServer.onNotFound(handleNotFound);       // captive davranis
  webServer.begin();

  Serial.println("Hazir. Telefonu agina baglayinca sayfa acilir.");
  Serial.println("Gelen mesajlar seri log'da [MESAJ], ayrica http://192.168.4.1/mesajlar");
}

void loop() {
  dnsServer.processNextRequest();
  webServer.handleClient();
}
