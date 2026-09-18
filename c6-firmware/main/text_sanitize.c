#include "text_sanitize.h"

#include <string.h>

void sanitize_wire_text(char *text)
{
    for (char *p = text; *p != '\0'; p++) {
        if (*p == ',' || *p == '\n' || *p == '\r') {
            *p = '_';
        }
    }
}

void extract_ble_name(const uint8_t *data, uint8_t len, char *out_name, size_t out_cap)
{
    out_name[0] = '\0';
    size_t i = 0;
    while (i + 1 < len) {
        uint8_t field_len = data[i];
        if (field_len == 0 || i + 1 + field_len > len) {
            break;
        }
        uint8_t field_type = data[i + 1];
        if (field_type == 0x09 || field_type == 0x08) {
            size_t name_len = field_len - 1;
            if (name_len >= out_cap) {
                name_len = out_cap - 1;
            }
            memcpy(out_name, &data[i + 2], name_len);
            out_name[name_len] = '\0';
            return;
        }
        i += 1 + field_len;
    }
}
