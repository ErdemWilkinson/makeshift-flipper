#include "json_escape.h"

void json_escape_append(char *out, size_t out_cap, size_t *out_len, const char *s)
{
    for (; *s != '\0' && *out_len < out_cap - 1; s++) {
        if (*s == '"' || *s == '\\') {
            if (*out_len >= out_cap - 2) {
                break;
            }
            out[(*out_len)++] = '\\';
        }
        out[(*out_len)++] = *s;
    }
    out[*out_len] = '\0';
}
