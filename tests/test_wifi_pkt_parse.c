// Host test for Wi-Fi monitor PKT line parsing & security classification
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "minitest.h"
#include "c6_link.h"

// Test mock parser logic matching handle_pkt_line in c6_link.c
typedef struct {
    char bssid_hex[13];
    char ssid[33];
    int rssi;
    int channel;
    char sec[8];
} parsed_pkt_t;

static bool parse_pkt_wire_line(const char *line, parsed_pkt_t *out)
{
    if (strncmp(line, "PKT:", 4) != 0) {
        return false;
    }
    const char *body = line + 4;
    if (strlen(body) < 12 || body[12] != ',') {
        return false;
    }
    strncpy(out->bssid_hex, body, 12);
    out->bssid_hex[12] = '\0';

    const char *ssid_start = body + 13;
    const char *comma1 = strchr(ssid_start, ',');
    if (!comma1) return false;
    const char *comma2 = strchr(comma1 + 1, ',');
    if (!comma2) return false;

    size_t ssid_len = comma1 - ssid_start;
    if (ssid_len > 32) ssid_len = 32;
    memcpy(out->ssid, ssid_start, ssid_len);
    out->ssid[ssid_len] = '\0';

    out->rssi = atoi(comma1 + 1);
    out->channel = atoi(comma2 + 1);

    const char *comma3 = strchr(comma2 + 1, ',');
    if (comma3) {
        strncpy(out->sec, comma3 + 1, sizeof(out->sec) - 1);
        out->sec[sizeof(out->sec) - 1] = '\0';
    } else {
        strcpy(out->sec, "?");
    }
    return true;
}

MT_TEST(pkt_parse_five_fields_with_security)
{
    parsed_pkt_t pkt;
    bool ok = parse_pkt_wire_line("PKT:112233445566,TestSSID,-65,6,WPA2", &pkt);
    MT_CHECK(ok);
    MT_CHECK_EQ_STR(pkt.bssid_hex, "112233445566");
    MT_CHECK_EQ_STR(pkt.ssid, "TestSSID");
    MT_CHECK_EQ_INT(pkt.rssi, -65);
    MT_CHECK_EQ_INT(pkt.channel, 6);
    MT_CHECK_EQ_STR(pkt.sec, "WPA2");
}

MT_TEST(pkt_parse_four_fields_legacy_backwards_compatible)
{
    parsed_pkt_t pkt;
    bool ok = parse_pkt_wire_line("PKT:AABBCCDDEEFF,LegacyNet,-80,11", &pkt);
    MT_CHECK(ok);
    MT_CHECK_EQ_STR(pkt.bssid_hex, "AABBCCDDEEFF");
    MT_CHECK_EQ_STR(pkt.ssid, "LegacyNet");
    MT_CHECK_EQ_INT(pkt.rssi, -80);
    MT_CHECK_EQ_INT(pkt.channel, 11);
    MT_CHECK_EQ_STR(pkt.sec, "?");
}

MT_TEST(pkt_parse_open_network)
{
    parsed_pkt_t pkt;
    bool ok = parse_pkt_wire_line("PKT:001122334455,FreeWiFi,-40,1,OPEN", &pkt);
    MT_CHECK(ok);
    MT_CHECK_EQ_STR(pkt.sec, "OPEN");
}

MT_TEST(pkt_parse_wpa3_network)
{
    parsed_pkt_t pkt;
    bool ok = parse_pkt_wire_line("PKT:001122334455,SecureNet,-50,36,WPA3", &pkt);
    MT_CHECK(ok);
    MT_CHECK_EQ_STR(pkt.sec, "WPA3");
}

int main(void)
{
    printf("test_wifi_pkt_parse:\n");
    MT_RUN(pkt_parse_five_fields_with_security);
    MT_RUN(pkt_parse_four_fields_legacy_backwards_compatible);
    MT_RUN(pkt_parse_open_network);
    MT_RUN(pkt_parse_wpa3_network);
    return MT_SUMMARY();
}
