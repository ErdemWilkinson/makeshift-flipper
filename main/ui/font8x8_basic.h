#pragma once

#include <stdint.h>

// 8x8 monochrome font, ASCII 0x00-0x7F. Each row is one byte (bit0 = leftmost pixel).
// Public-domain font data (font8x8, Daniel Hepper).
extern const uint8_t font8x8_basic[128][8];
