#include "pkt_line_parse.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

bool pkt_line_parse(const char *line, uint8_t out_bssid[6],
                     char *out_ssid, size_t ssid_cap,
                     int *out_rssi, int *out_channel,
                     char *out_sec, size_t sec_cap)
{
    if (strncmp(line, "PKT:", 4) != 0) {
        return false;
    }
    const char *body = line + 4; // skip "PKT:"

    if (strlen(body) < 12 || body[12] != ',') {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        unsigned int byte;
        if (sscanf(body + i * 2, "%2x", &byte) != 1) {
            return false;
        }
        out_bssid[i] = (uint8_t)byte;
    }

    const char *ssid_start = body + 13;
    const char *comma1 = strchr(ssid_start, ',');
    if (comma1 == NULL) {
        return false;
    }
    const char *comma2 = strchr(comma1 + 1, ',');
    if (comma2 == NULL) {
        return false;
    }

    size_t ssid_len = (size_t)(comma1 - ssid_start);
    if (ssid_len > ssid_cap - 1) {
        ssid_len = ssid_cap - 1;
    }
    memcpy(out_ssid, ssid_start, ssid_len);
    out_ssid[ssid_len] = '\0';

    *out_rssi = atoi(comma1 + 1);
    *out_channel = atoi(comma2 + 1);

    const char *comma3 = strchr(comma2 + 1, ',');
    const char *sec = comma3 ? (comma3 + 1) : "?";
    strncpy(out_sec, sec, sec_cap - 1);
    out_sec[sec_cap - 1] = '\0';
    return true;
}
