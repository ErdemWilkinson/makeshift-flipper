#pragma once

#include <stdbool.h>

// UART link to the P4. This side is the "server": it blocks waiting for
// one line at a time from the P4 and dispatches it; replies are sent back
// the same way. See main/net/c6_link.h on the P4 side for the protocol.

#define UART_LINK_MAX_LINE_LEN 256

// Brings up the UART. Call once at startup.
void uart_link_init(void);

// Blocks until a full newline-terminated line arrives from the P4.
// Copies it (newline stripped) into `out_line` (capacity UART_LINK_MAX_LINE_LEN).
void uart_link_read_line(char *out_line);

// Sends one line to the P4, appending the newline.
void uart_link_write_line(const char *line);
