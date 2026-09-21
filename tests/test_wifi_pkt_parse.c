// Host test for Wi-Fi monitor PKT line parsing & security classification.
// Includes the real production source (main/net/pkt_line_parse.c) directly,
// same pattern as every other test in this suite -- this is not a copy of
// the parser, it's the exact function c6_link.c's handle_pkt_line() calls.
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "minitest.h"
#include "../main/net/pkt_line_parse.c"

MT_TEST(pkt_parse_five_fields_with_security)
{
    uint8_t bssid[6];
    char ssid[33];
    int rssi, channel;
    char sec[8];
    bool ok = pkt_line_parse("PKT:112233445566,TestSSID,-65,6,WPA2",
                              bssid, ssid, sizeof(ssid), &rssi, &channel,
                              sec, sizeof(sec));
    MT_CHECK(ok);
    MT_CHECK_EQ_INT(bssid[0], 0x11);
    MT_CHECK_EQ_INT(bssid[5], 0x66);
    MT_CHECK_EQ_STR(ssid, "TestSSID");
    MT_CHECK_EQ_INT(rssi, -65);
    MT_CHECK_EQ_INT(channel, 6);
    MT_CHECK_EQ_STR(sec, "WPA2");
}

MT_TEST(pkt_parse_four_fields_legacy_backwards_compatible)
{
    uint8_t bssid[6];
    char ssid[33];
    int rssi, channel;
    char sec[8];
    bool ok = pkt_line_parse("PKT:AABBCCDDEEFF,LegacyNet,-80,11",
                              bssid, ssid, sizeof(ssid), &rssi, &channel,
                              sec, sizeof(sec));
    MT_CHECK(ok);
    MT_CHECK_EQ_INT(bssid[0], 0xAA);
    MT_CHECK_EQ_STR(ssid, "LegacyNet");
    MT_CHECK_EQ_INT(rssi, -80);
    MT_CHECK_EQ_INT(channel, 11);
    MT_CHECK_EQ_STR(sec, "?");
}

MT_TEST(pkt_parse_open_network)
{
    uint8_t bssid[6];
    char ssid[33];
    int rssi, channel;
    char sec[8];
    bool ok = pkt_line_parse("PKT:001122334455,FreeWiFi,-40,1,OPEN",
                              bssid, ssid, sizeof(ssid), &rssi, &channel,
                              sec, sizeof(sec));
    MT_CHECK(ok);
    MT_CHECK_EQ_STR(sec, "OPEN");
}

MT_TEST(pkt_parse_wpa3_network)
{
    uint8_t bssid[6];
    char ssid[33];
    int rssi, channel;
    char sec[8];
    bool ok = pkt_line_parse("PKT:001122334455,SecureNet,-50,36,WPA3",
                              bssid, ssid, sizeof(ssid), &rssi, &channel,
                              sec, sizeof(sec));
    MT_CHECK(ok);
    MT_CHECK_EQ_STR(sec, "WPA3");
}

MT_TEST(pkt_parse_rejects_missing_prefix)
{
    uint8_t bssid[6];
    char ssid[33];
    int rssi, channel;
    char sec[8];
    bool ok = pkt_line_parse("NOTPKT:112233445566,X,-65,6",
                              bssid, ssid, sizeof(ssid), &rssi, &channel,
                              sec, sizeof(sec));
    MT_CHECK(!ok);
}

MT_TEST(pkt_parse_rejects_short_or_malformed_bssid)
{
    uint8_t bssid[6];
    char ssid[33];
    int rssi, channel;
    char sec[8];
    bool ok = pkt_line_parse("PKT:1122,X,-65,6",
                              bssid, ssid, sizeof(ssid), &rssi, &channel,
                              sec, sizeof(sec));
    MT_CHECK(!ok);
}

MT_TEST(pkt_parse_truncates_ssid_to_output_capacity)
{
    uint8_t bssid[6];
    char ssid[5]; // capacity 5 -> 4 chars + NUL
    int rssi, channel;
    char sec[8];
    bool ok = pkt_line_parse("PKT:001122334455,VeryLongSSIDName,-50,1,OPEN",
                              bssid, ssid, sizeof(ssid), &rssi, &channel,
                              sec, sizeof(sec));
    MT_CHECK(ok);
    MT_CHECK_EQ_INT((int)strlen(ssid), 4);
}

int main(void)
{
    printf("test_wifi_pkt_parse:\n");
    MT_RUN(pkt_parse_five_fields_with_security);
    MT_RUN(pkt_parse_four_fields_legacy_backwards_compatible);
    MT_RUN(pkt_parse_open_network);
    MT_RUN(pkt_parse_wpa3_network);
    MT_RUN(pkt_parse_rejects_missing_prefix);
    MT_RUN(pkt_parse_rejects_short_or_malformed_bssid);
    MT_RUN(pkt_parse_truncates_ssid_to_output_capacity);
    return MT_SUMMARY();
}
