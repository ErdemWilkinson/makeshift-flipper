#pragma once

#include <stddef.h>

// Appends `s` to `out` (which already has `*out_len` bytes written),
// escaping '"' and '\' for embedding as a JSON string value, and keeps the
// result null-terminated and within `out_cap`. Used by c6_link.c to build
// LOGSEND: lines from diag_entry_t module/code fields -- pulled out as its
// own module so it can be host-tested without the UART/FreeRTOS plumbing
// the rest of c6_link.c depends on.
void json_escape_append(char *out, size_t out_cap, size_t *out_len, const char *s);
