#pragma once

// Pin plan for the Waveshare ESP32-C6-DEV-KIT-NX (full map: WIRING_MAP_RC522.md).
// Never wire GPIO12/13 (native USB D-/D+) or GPIO16/17 (UART0 to the CH343).
// Strapping pins (4, 5, 15) only carry inputs whose source idles high or
// high-Z, so no module can pull a strap pin low at reset.
#define BOARD_UP_GPIO 6
#define BOARD_RC522_MISO_GPIO 5
#define BOARD_HAS_RC522 0

#if BOARD_RC522_MISO_GPIO == BOARD_UP_GPIO
#error "RC522 MISO and joystick UP cannot share a GPIO"
#endif
