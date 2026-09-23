#pragma once

#include <stdint.h>

// 8x16 monochrome font, ASCII 0x00-0x7F. Each row is one byte (bit0 =
// leftmost pixel), 16 rows per glyph. Unmapped/control codes render blank.
// See main/ui/font8x16_basic.c's header comment for its origin.
extern const uint8_t font8x16_basic[128][16];
