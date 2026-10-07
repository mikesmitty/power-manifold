#pragma once

// mbedTLS 3.6 for the broker link (src/net/mqtt_tls.c), a TLS 1.2 client,
// and the web server's HTTPS (src/net/https.c), a TLS 1.2 server with an
// ECDSA certificate. The update client stays on plain HTTP by design, with
// firmware authenticity on the image signature. Measured 2026-10-04 against
// the same SDK: the client alone added about 105 KB of code and 9 KB of RAM.

#define MBEDTLS_ALLOW_PRIVATE_ACCESS // the SDK's lwIP glue reaches into mbedTLS structs

#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT // pico_mbedtls.c feeds the hardware generator
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

// Certificate dates are judged against time(), which net.c sets from SNTP.
// The millisecond clock mbedTLS also wants comes from mqtt_tls.c.
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE
#define MBEDTLS_PLATFORM_MS_TIME_ALT
#define MBEDTLS_PLATFORM_C

#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C

#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_SRV_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
// Session tickets let a browser resume a session without the key exchange
// and signature, which cost the controller a few hundred milliseconds each
// (lwipopts.h ALTCP_MBEDTLS_USE_SESSION_TICKETS).
#define MBEDTLS_SSL_SESSION_TICKETS
#define MBEDTLS_SSL_TICKET_C
// Buffers are per connection and sized for the largest record each way.
// Browsers and curl send records of up to 16 KB (a firmware upload does)
// and TLS 1.2 gives a server no way to ask for less, so receiving takes the
// full 16 KB. Sending must hold the server's whole certificate chain as one
// handshake message: up to 8 KB of it (net/tls_bundle.h TLS_RECORD_MAX).
// The broker link has the same sizes, which also covers a broker's chain.
#define MBEDTLS_SSL_IN_CONTENT_LEN  16384
#define MBEDTLS_SSL_OUT_CONTENT_LEN 8192

// Key exchange and signatures: ECDHE with either an ECDSA or an RSA
// certificate, P-256 and P-384. RSA costs 22 KB and stays because most
// home-rolled CAs are RSA; the web server accepts ECDSA keys only, since
// an RSA signature would take it over a second per connection.
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15

// Record protection and hashes: AES-GCM, SHA-256 and SHA-384
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C
