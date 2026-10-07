#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The web server's HTTPS certificate as it arrives and as it is stored.
// Hardware-free, so the host tests cover it; the device checks the key and
// the certificates with mbedTLS afterwards (net/https.h).
//
// It arrives as one PEM text: the private key and the certificate chain in
// any order, leaf first among the certificates, the way acme.sh's key and
// fullchain files read when joined. It is stored as DER in one flash
// record of TLS_RECORD_MAX bytes.

// Let's Encrypt's ECDSA chain is four today (leaf, YE1, Root YE cross-signed
// by X2, X2 cross-signed by X1); two more leave room for another cross-sign
#define TLS_CERTS_MAX  6
#define TLS_RECORD_MAX 8192 // one record: header, key and chain

typedef struct {
    const uint8_t *key;
    uint16_t key_len;
    uint8_t n_certs;
    const uint8_t *cert[TLS_CERTS_MAX]; // cert[0] is the leaf
    uint16_t cert_len[TLS_CERTS_MAX];
} tls_bundle_t;

// Split PEM text into DER: the blocks are decoded into der (cap bytes) and
// b points into it. Blocks other than a certificate or a private key are
// skipped. NULL on success, else a message for the person who sent it.
const char *tls_bundle_parse(const char *pem, size_t len, uint8_t *der, size_t cap,
                             tls_bundle_t *b);

// The bundle as a flash record, seq numbering the saves. Returns its
// length, or 0 when it does not fit cap.
size_t tls_record_encode(const tls_bundle_t *b, uint32_t seq, uint8_t *out, size_t cap);

// A stored record read back: b points into rec. False for erased flash, a
// torn write or anything else that is not a whole record.
bool tls_record_decode(const uint8_t *rec, size_t cap, tls_bundle_t *b, uint32_t *seq);

// name (any case, n bytes) is covered by the certificate name pattern: the
// same name, or "*.rest" matching exactly one label in front of rest.
bool tls_name_match(const char *pattern, size_t plen, const char *name, size_t n);
