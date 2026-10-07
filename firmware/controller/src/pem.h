#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// PEM and DER for one X.509 certificate. Hardware-free, so the settings JSON
// code and the host tests share it; the device validates the DER with mbedTLS
// afterwards (net/mqtt_tls.h).

// Decode a certificate given as PEM text into DER. The BEGIN and END lines
// are optional, so bare base64 works too; whitespace anywhere is ignored, and
// decoding stops at the first END line, so the first certificate of a pasted
// chain is the one taken. Returns the DER length, or 0 when the text holds
// no base64, holds a character that is not base64, or does not fit cap.
size_t pem_to_der(const char *text, uint8_t *out, size_t cap);

// Decode the base64 in text[0..len) into out, skipping whitespace and
// stopping at padding. Returns the byte count, or 0 when the text holds no
// base64, holds a character that is not base64, or does not fit cap.
size_t pem_base64_decode(const char *text, size_t len, uint8_t *out, size_t cap);

// Encode DER as PEM text: the CERTIFICATE armour around base64 wrapped at 64
// columns, every line ended with eol. eol is "\n" for a file and "\\n" (the
// two characters) when writing straight into a JSON string. Returns the
// length written excluding the NUL, or 0 when it does not fit cap.
size_t der_to_pem(const uint8_t *der, size_t len, char *out, size_t cap, const char *eol);

// The longest PEM text (newline line ends, NUL included) a DER of der_max
// bytes can produce
#define PEM_CERT_TEXT_MAX(der_max) \
    (28 + ((der_max) + 2) / 3 * 4 + (((der_max) + 2) / 3 * 4 + 63) / 64 + 26 + 1)

// True when buf looks like one DER certificate: a SEQUENCE whose stated
// length fills the buffer exactly.
bool der_cert_shape_ok(const uint8_t *der, size_t len);
