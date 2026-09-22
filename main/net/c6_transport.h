#pragma once

// Skeleton for a future low-level P4<->C6 UART transport layer -- the
// byte/line-level send/receive plumbing (uart_write_bytes/read_bytes,
// line buffering, timeouts) that main/net/c6_link.c currently owns
// directly (send_line()/read_line() and friends). This file is
// intentionally empty of real declarations until it's actually split
// out of c6_link.c; wifi_scan_client.h (and any other future protocol
// client) would depend on this rather than re-implementing UART I/O.
//
// If/when this split happens: move c6_link.c's UART init and
// send_line()/read_line() here, keep c6_link.c (and wifi_scan_client.c)
// as callers of this transport rather than direct uart_* API users, and
// add "net/c6_transport.c" to main/CMakeLists.txt's SRCS list.
