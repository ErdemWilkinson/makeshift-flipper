#pragma once

// Skeleton for future Wi-Fi research/utility functions, on top of the
// existing scan/monitor/setup API in c6_link.h (main/net/c6_link.h) --
// this file is intentionally empty of real declarations until a specific
// feature is defined. Keep radio-specific ESP32-C6 code in c6-firmware/;
// this file is the P4-side orchestration/UI-facing surface, matching how
// c6_link.c already wraps the UART protocol rather than talking to the
// C6's radios directly.
//
// When a feature is specified, add its prototypes here (following
// c6_link.h's doc-comment style) and add "net/wifi_tools.c" to
// main/CMakeLists.txt's SRCS list.
