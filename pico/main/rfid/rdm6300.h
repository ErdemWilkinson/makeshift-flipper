#pragma once

#include <stdbool.h>
#include <stdint.h>

// Driver for the RDM6300 125kHz RFID reader. It's a read-only, UART-output
// module: whenever a tag is in range it spontaneously sends a fixed 14-byte
// ASCII frame at 9600 baud. There's no command protocol to talk back to it.

// EM4100-family tags carry a 5-byte (40-bit) ID.
typedef struct {
    uint8_t bytes[5];
} rdm6300_id_t;

// Starts the UART configured for the RDM6300's fixed 9600 8N1 framing.
// Call once at startup.
void rdm6300_init(void);

// Returns true and fills `out_id` if a complete, checksum-valid tag frame
// has arrived since the last call. Genuinely non-blocking: it only reads
// bytes already sitting in the UART FIFO (zero timeout) and carries any
// partial frame across calls, so it never waits for more bytes to arrive.
// Safe to call every loop tick (e.g. alongside buttons_poll()).
bool rdm6300_poll(rdm6300_id_t *out_id);
