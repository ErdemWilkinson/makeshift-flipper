#pragma once

// Skeleton for future RFID/NFC research/utility functions, on top of the
// existing rc522.h (13.56MHz)/rdm6300.h (125kHz) drivers and
// rfid_library.h's saved-tag storage -- this file is intentionally empty
// of real declarations until a specific feature is defined. Keep the raw
// SPI/UART protocol code in rc522.c/rdm6300.c; this file is for
// higher-level tooling built on top of them (e.g. batch operations,
// analysis, or anything that doesn't belong in the read/write primitives
// or the persisted library).
//
// When a feature is specified, add its prototypes here (following
// rfid_library.h's doc-comment style) and add "rfid/rfid_tools.c" to
// main/CMakeLists.txt's SRCS list.
