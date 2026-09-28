#pragma once

// Current assembled device: LCD and its controls only. The joystick UP lead
// is on C6 GPIO16 (moved off GPIO6, which RC522 needs for SPI MISO). GPIO16
// is otherwise unclaimed on this board and is not a strapping pin, so it's a
// safe home for UP now that GPIO6 is reserved for RC522. Enabling RC522 still
// requires physically rewiring UP's lead to GPIO16 and MISO to GPIO6, then
// flipping BOARD_HAS_RC522 to 1 -- this #define alone does not move a wire.
#define BOARD_UP_GPIO 16
#define BOARD_HAS_RC522 0

#if BOARD_HAS_RC522 && BOARD_UP_GPIO == 6
#error "GPIO6 cannot serve both joystick UP and RC522 SPI MISO"
#endif
