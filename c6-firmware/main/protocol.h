#pragma once

// Skeleton for a future shared P4<->C6 line-protocol layer on the C6
// side -- the command-line reading/dispatch that main.c's main loop
// currently owns directly (uart_link_read_line(), the strcmp/strncmp
// chain routing to wifi_commands_*()/wifi_monitor_*()/bt_scan_*()), plus
// the reply-line writing wifi_commands.c/wifi_monitor.c/bt_scan.c each
// call uart_link_write_line() for directly today. This file is
// intentionally empty of real declarations until it's actually split
// out of main.c; see main/net/c6_transport.h for the equivalent
// P4-side split, if that happens too -- the two are independent changes
// (this file's counterpart is uart_link.c, not c6_transport.c).
//
// If/when this split happens: move main.c's command-dispatch loop body
// here, update main.c to call into it, and add "protocol.c" to
// c6-firmware/main/CMakeLists.txt's SRCS list.
