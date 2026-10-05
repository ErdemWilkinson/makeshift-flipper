// Captive-portal awareness screen for the standalone C6 firmware.
// See captive_portal.h for the scope and the (deliberate) limits.

#include "captive_portal.h"
#include "captive_cat_img.h"   // CAPTIVE_CAT_B64[]

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"

static const char *TAG = "captive";

// --- Moderator (isim/soyisim) girisi. Kullanici adi = isim, sifre = soyisim.
#define MOD_USER "erdem"
#define MOD_PASS "wilkinson"

#define AP_SSID_CAPTIVE "Bedava-WiFi"
#define CAPTIVE_AP_IP    "192.168.4.1"

// Gelen mesajlar (RAM, kalici degil).
#define MSG_KEEP 20
#define MSG_MAXLEN 200
static char s_msgs[MSG_KEEP][MSG_MAXLEN + 1];
static int  s_msg_count = 0;
static unsigned int s_msg_total = 0;

static bool s_running = false;
static char s_ssid[33] = {0};
static httpd_handle_t s_httpd = NULL;
static TaskHandle_t s_dns_task = NULL;
static volatile bool s_dns_run = false;

// forward
extern bool c6_link_ap_open_start(const char *ssid); // eklenecek (c6_link.c)
extern void c6_link_ap_open_stop(void);

// ---------------- DNS hijack (UDP 53) ----------------
// Her sorguya "tum isimler -> AP IP" cevabi doner, boylece istemci captive'e duser.
static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "DNS socket olusturulamadi");
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "DNS bind basarisiz");
        close(sock);
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t buf[512];
    ip4_addr_t apip;
    ip4addr_aton(CAPTIVE_AP_IP, &apip);

    while (s_dns_run) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
        if (n < (int)sizeof(uint16_t) * 6) continue; // en az DNS basligi
        // Basit DNS cevabi: soruyu aynen dondur + tek A kaydi (AP IP).
        // Header'i cevaba cevir.
        buf[2] |= 0x80;          // QR=1 (response)
        buf[3] = 0x80;           // RA=1
        buf[6] = 0x00; buf[7] = 0x01; // ANCOUNT=1
        buf[8] = 0x00; buf[9] = 0x00; // NSCOUNT=0
        buf[10] = 0x00; buf[11] = 0x00; // ARCOUNT=0

        int qlen = n; // sorunun sonu = paketin sonu (tek soru varsayimi)
        if (qlen + 16 > (int)sizeof(buf)) continue;
        uint8_t *p = buf + qlen;
        *p++ = 0xC0; *p++ = 0x0C;          // isim: header'daki soruya pointer
        *p++ = 0x00; *p++ = 0x01;          // TYPE A
        *p++ = 0x00; *p++ = 0x01;          // CLASS IN
        *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x3C; // TTL 60
        *p++ = 0x00; *p++ = 0x04;          // RDLENGTH 4
        uint32_t a = apip.addr;            // network byte order
        memcpy(p, &a, 4); p += 4;

        int outlen = p - buf;
        sendto(sock, buf, outlen, 0, (struct sockaddr *)&from, flen);
    }
    close(sock);
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

// ---------------- HTML ----------------
static const char *CSS =
":root{--bg:#0f1115;--card:#171a21;--line:#242833;--text:#e7e9ee;--dim:#9aa1ad;"
"--accent:#f5b301;--ok:#2dd4a7;--warn:#ff6b6b}"
"*{box-sizing:border-box}"
"body{margin:0;font-family:system-ui,Segoe UI,Roboto,Arial,sans-serif;"
"background:radial-gradient(1200px 600px at 50% -10%,#1a1f2b 0%,var(--bg) 60%);"
"color:var(--text);min-height:100vh;display:flex;flex-direction:column;align-items:center;padding:20px 16px 40px}"
".wrap{width:100%;max-width:440px;margin:0 auto;text-align:center}"
".hero{position:relative;border-radius:18px;overflow:hidden;border:1px solid var(--line);"
"box-shadow:0 12px 40px rgba(0,0,0,.45);margin-bottom:12px}"
".hero img{display:block;width:100%;height:auto}"
".badge{position:absolute;top:12px;left:12px;background:rgba(15,17,21,.72);"
"border:1px solid var(--line);color:var(--accent);font-size:12px;font-weight:700;"
"letter-spacing:.5px;padding:6px 10px;border-radius:999px}"
".card{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:18px;text-align:center}"
"h1{font-size:20px;margin:0 0 8px}h1 .dot{color:var(--accent)}"
"p.note{font-size:13px;color:var(--dim);opacity:.6;margin:0 0 14px}"
"textarea{width:100%;min-height:96px;border-radius:12px;border:1px solid var(--line);"
"background:#0d0f14;color:var(--text);padding:12px;font-size:15px;text-align:left;outline:none}"
"textarea:focus{border-color:var(--accent)}"
"button{margin-top:12px;width:100%;padding:13px;border:0;border-radius:12px;"
"background:linear-gradient(180deg,var(--ok),#1fb692);color:#04140d;font-weight:800;font-size:16px}"
".warn{margin-top:18px;padding:12px 14px;border-radius:12px;background:rgba(255,107,107,.10);"
"border:1px solid rgba(255,107,107,.35);color:var(--warn);font-weight:700;font-size:14px}"
"a{color:var(--ok);text-decoration:none}";

// HTML kacir (mesaj sayfayi bozmasin).
static void html_escape_to(httpd_req_t *req, const char *s)
{
    char tmp[16];
    for (; *s; s++) {
        switch (*s) {
            case '&': httpd_resp_sendstr_chunk(req, "&amp;"); break;
            case '<': httpd_resp_sendstr_chunk(req, "&lt;"); break;
            case '>': httpd_resp_sendstr_chunk(req, "&gt;"); break;
            case '"': httpd_resp_sendstr_chunk(req, "&quot;"); break;
            default: { tmp[0] = *s; tmp[1] = '\0'; httpd_resp_sendstr_chunk(req, tmp); }
        }
    }
}

// Ana sayfa (ziyaretci)
static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html lang='tr'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Bedava WiFi</title><style>");
    httpd_resp_sendstr_chunk(req, CSS);
    httpd_resp_sendstr_chunk(req, "</style></head><body><div class='wrap'>"
        "<div class='hero'><span class='badge'>BILGILENDIRME</span>"
        "<img alt='SEN / BEDAVA WIFI' src='data:image/jpeg;base64,");
    httpd_resp_sendstr_chunk(req, CAPTIVE_CAT_B64);
    httpd_resp_sendstr_chunk(req, "'></div>"
        "<p class='note'>&quot;saka&quot;</p>"
        "<div class='card'><h1>Bir mesaj birak<span class='dot'>.</span></h1>"
        "<form method='POST' action='/msg'>"
        "<textarea name='m' maxlength='200' placeholder='Buraya yaz...'></textarea>"
        "<button type='submit'>Gonder</button></form></div>"
        "<div class='warn'>&#9888; Her WiFi'ye guvenme.</div>"
        "</div></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

// URL-decode (application/x-www-form-urlencoded) -> "m" alanini cek.
static void extract_m(const char *body, char *out, size_t out_cap)
{
    out[0] = '\0';
    const char *p = strstr(body, "m=");
    if (!p) return;
    if (p != body && *(p - 1) != '&') {
        // "m=" baska bir alan adinin sonu olabilir; basit kontrol
    }
    p += 2;
    size_t o = 0;
    for (; *p && *p != '&' && o + 1 < out_cap; p++) {
        char c = *p;
        if (c == '+') c = ' ';
        else if (c == '%') {
            int hi, lo;
            if (p[1] && p[2] &&
                sscanf(p + 1, "%1x", &hi) == 1 && sscanf(p + 2, "%1x", &lo) == 1) {
                c = (char)((hi << 4) | lo);
                p += 2;
            }
        }
        out[o++] = c;
    }
    out[o] = '\0';
}

static esp_err_t msg_post(httpd_req_t *req)
{
    int total = req->content_len;
    if (total > 512) total = 512;
    char body[513];
    int got = 0;
    while (got < total) {
        int r = httpd_req_recv(req, body + got, total - got);
        if (r <= 0) break;
        got += r;
    }
    body[got] = '\0';

    char m[MSG_MAXLEN + 1];
    extract_m(body, m, sizeof(m));
    // trim
    char *st = m; while (*st == ' ') st++;
    if (*st) {
        if (s_msg_count < MSG_KEEP) {
            strncpy(s_msgs[s_msg_count], st, MSG_MAXLEN);
            s_msgs[s_msg_count][MSG_MAXLEN] = '\0';
            s_msg_count++;
        } else {
            for (int i = 1; i < MSG_KEEP; i++) strcpy(s_msgs[i - 1], s_msgs[i]);
            strncpy(s_msgs[MSG_KEEP - 1], st, MSG_MAXLEN);
            s_msgs[MSG_KEEP - 1][MSG_MAXLEN] = '\0';
        }
        s_msg_total++;
        ESP_LOGI(TAG, "[MESAJ] %s", st);
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html lang='tr'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Tesekkurler</title><style>");
    httpd_resp_sendstr_chunk(req, CSS);
    httpd_resp_sendstr_chunk(req, "</style></head><body><div class='wrap'>"
        "<div class='card'><h1>Tesekkurler<span class='dot'>.</span></h1>"
        "<p class='note' style='opacity:1'>Mesajin alindi.</p>"
        "<p><a href='/'>&larr; Geri don</a></p></div>"
        "<div class='warn'>&#9888; Her WiFi'ye guvenme.</div>"
        "</div></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

// Basic Auth kontrolu: isim (kullanici) + soyisim (sifre).
static bool check_auth(httpd_req_t *req)
{
    char hdr[128];
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK)
        return false;
    // "Basic base64(user:pass)" -> beklenen erdem:wilkinson
    // Kucuk kod: bilinen dogru degeri onceden base64'leyip karsilastiralim.
    // erdem:wilkinson -> ZXJkZW06d2lsa2luc29u
    return strstr(hdr, "ZXJkZW06d2lsa2luc29u") != NULL;
}

static esp_err_t mesajlar_get(httpd_req_t *req)
{
    if (!check_auth(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate",
                           "Basic realm=\"Isim (kullanici) + soyisim (sifre)\"");
        httpd_resp_sendstr(req, "Giris gerekli");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html lang='tr'><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<meta http-equiv='refresh' content='5'><title>Mesajlar</title><style>");
    httpd_resp_sendstr_chunk(req, CSS);
    httpd_resp_sendstr_chunk(req, "</style></head><body><div class='wrap'>"
        "<div class='card'><h1>Mesajlar<span class='dot'>.</span></h1>"
        "<p class='note' style='opacity:1'>Toplam: ");
    char num[16];
    snprintf(num, sizeof(num), "%u", s_msg_total);
    httpd_resp_sendstr_chunk(req, num);
    httpd_resp_sendstr_chunk(req, "</p>");

    if (s_msg_count == 0) {
        httpd_resp_sendstr_chunk(req,
            "<p class='note' style='opacity:1'>Henuz mesaj yok.</p>");
    } else {
        for (int i = s_msg_count - 1; i >= 0; i--) {
            httpd_resp_sendstr_chunk(req,
                "<div style='padding:10px 12px;margin:8px 0;border-radius:10px;"
                "background:#0d0f14;border:1px solid var(--line);text-align:left'>");
            html_escape_to(req, s_msgs[i]);
            httpd_resp_sendstr_chunk(req, "</div>");
        }
    }
    httpd_resp_sendstr_chunk(req, "</div></div></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

// Captive yakalama: bilinmeyen her istegi ana sayfaya 302 yonlendir.
// AMA once bilinen yollari (/mesajlar, /msg, /) elle kontrol et -- wildcard
// eslemesi bazen bu yollari da buraya dusurebiliyor (ozellikle iPhone'un
// captive kontrol istekleri + ?query ekleri yuzunden). Boylece /mesajlar
// hangi yolla gelirse gelsin dogru sayfaya gider.
static esp_err_t redirect_get(httpd_req_t *req)
{
    // URI'nin yol kismini query'den (?...) ayir.
    const char *uri = req->uri;
    if (strncmp(uri, "/mesajlar", 9) == 0 &&
        (uri[9] == '\0' || uri[9] == '?' || uri[9] == '/')) {
        return mesajlar_get(req);
    }
    if (strcmp(uri, "/") == 0) {
        return root_get(req);
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" CAPTIVE_AP_IP "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ---------------- baslat / durdur ----------------
bool captive_portal_start(void)
{
    if (s_running) return true;

    if (!c6_link_ap_open_start(AP_SSID_CAPTIVE)) {
        ESP_LOGE(TAG, "acik AP baslatilamadi");
        return false;
    }
    snprintf(s_ssid, sizeof(s_ssid), "%s", AP_SSID_CAPTIVE);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 8;
    cfg.lru_purge_enable = true;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "http baslatilamadi");
        c6_link_ap_open_stop();
        return false;
    }
    httpd_uri_t u_root = { .uri = "/", .method = HTTP_GET, .handler = root_get };
    httpd_uri_t u_msg  = { .uri = "/msg", .method = HTTP_POST, .handler = msg_post };
    httpd_uri_t u_mes  = { .uri = "/mesajlar", .method = HTTP_GET, .handler = mesajlar_get };
    httpd_uri_t u_any  = { .uri = "/*", .method = HTTP_GET, .handler = redirect_get };
    httpd_register_uri_handler(s_httpd, &u_root);
    httpd_register_uri_handler(s_httpd, &u_msg);
    httpd_register_uri_handler(s_httpd, &u_mes);
    httpd_register_uri_handler(s_httpd, &u_any);

    s_dns_run = true;
    xTaskCreate(dns_task, "captive_dns", 4096, NULL, 5, &s_dns_task);

    s_running = true;
    ESP_LOGI(TAG, "captive portal basladi: SSID=%s", s_ssid);
    return true;
}

void captive_portal_stop(void)
{
    if (!s_running) return;
    s_dns_run = false;
    // DNS task 1sn timeout ile kendini kapatir.
    for (int i = 0; i < 20 && s_dns_task != NULL; i++) vTaskDelay(pdMS_TO_TICKS(100));
    if (s_httpd) { httpd_stop(s_httpd); s_httpd = NULL; }
    c6_link_ap_open_stop();
    s_running = false;
    s_ssid[0] = '\0';
    ESP_LOGI(TAG, "captive portal durdu");
}

bool captive_portal_is_running(void) { return s_running; }
const char *captive_portal_ssid(void) { return s_ssid; }
unsigned int captive_portal_msg_total(void) { return s_msg_total; }
