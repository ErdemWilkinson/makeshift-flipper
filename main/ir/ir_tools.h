#pragma once

// Skeleton for future IR research/utility functions, on top of the
// existing ir_nec.h (protocol decode/encode), ir_driver.h (RMT RX/TX),
// and ir_library.h (saved-code storage) -- this file is intentionally
// empty of real declarations until a specific feature is defined. Keep
// the RMT/hardware capture-replay code in ir_driver.c; this file is for
// higher-level tooling built on top of the existing driver/library API
// (e.g. batch operations or analysis that doesn't belong in the
// send/receive primitives or the persisted library).
//
// When a feature is specified, add its prototypes here (following
// ir_library.h's doc-comment style) and add "ir/ir_tools.c" to
// main/CMakeLists.txt's SRCS list.
