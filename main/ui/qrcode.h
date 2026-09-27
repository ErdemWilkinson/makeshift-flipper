#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Minimal QR Code generator (byte mode, ECC level M, versions 1-10).
// Enough to encode a Wi-Fi join string; not a general-purpose library.
// Public-domain algorithm (after Nayuki's QR-Code-generator), reimplemented
// compactly for this firmware. No dynamic allocation: the caller supplies the
// module buffer, sized for the largest version this build supports.

// Version 10 is 57x57 modules; one byte per module keeps it simple.
#define QR_MAX_VERSION 10
#define QR_MAX_SIZE (17 + 4 * QR_MAX_VERSION) // 57

typedef struct {
    int size;                       // side length in modules (0 if encode failed)
    uint8_t modules[QR_MAX_SIZE * QR_MAX_SIZE]; // 1 = dark, 0 = light
} qr_code_t;

// Encodes `text` (a NUL-terminated byte string) into `out`. Returns false if
// the text does not fit in QR_MAX_VERSION at ECC level M. On success out->size
// is the module count per side and out->modules holds the grid row-major.
bool qr_encode(qr_code_t *out, const char *text);

// Convenience: builds the standard Wi-Fi join payload
//   WIFI:T:WPA;S:<ssid>;P:<password>;;
// (with ; , : \ escaped per the de-facto format) and encodes it.
bool qr_encode_wifi(qr_code_t *out, const char *ssid, const char *password);
