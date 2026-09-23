#pragma once

#include <stddef.h>

// Appends `s` to `out` (which already has `*out_len` bytes written),
// escaping '"' and '\' for embedding as a JSON string value, and keeps the
// result null-terminated and within `out_cap`.
//
// Not used by any current production caller: it backed the old two-chip
// design's LOGSEND: wire-protocol lines, which c6_link.c no longer sends
// (see KNOWN_ISSUES.md Round 27 and c6_link_send_error_log()'s comment).
// Kept only because tests/test_json_escape.c host-tests it directly
// (#include "../main/net/json_escape.c") and it's a small, harmless,
// still-correct utility -- not built into the firmware image
// (main/CMakeLists.txt's SRCS list omits this .c file). Reuse it if a
// future feature needs JSON string escaping; otherwise safe to delete
// along with its test once nothing references it.
void json_escape_append(char *out, size_t out_cap, size_t *out_len, const char *s);
