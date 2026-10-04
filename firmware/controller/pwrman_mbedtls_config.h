#pragma once

// mbedTLS 3.6 for the broker link only (src/net/mqtt_tls.c): a TLS 1.2
// client that trusts the one certificate in the settings, or nothing in the
// unverified state. The update client stays on plain HTTP by design, with
// firmware authenticity on the image signature. Measured 2026-10-04 against
// the same SDK: this selection adds about 105 KB of code and 9 KB of RAM.

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
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_SERVER_NAME_INDICATION
// The receive buffer must hold the broker's whole certificate chain as one
// handshake message: a leaf plus one intermediate with RSA keys is about
// 4 KB. Nothing the controller sends over MQTT comes near the send buffer.
#define MBEDTLS_SSL_IN_CONTENT_LEN  8192
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096

// Key exchange and signatures: ECDHE with either an ECDSA or an RSA
// certificate, P-256 and P-384. RSA costs 22 KB and stays because most
// home-rolled CAs are RSA.
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
