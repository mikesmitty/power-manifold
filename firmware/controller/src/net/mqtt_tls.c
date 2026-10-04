#include "mqtt_tls.h"

#include <stdio.h>
#include <string.h>

#include "pico/time.h"

#include "lwip/altcp_tls.h"
#include "lwip/apps/mqtt.h"
#include "lwip/apps/mqtt_priv.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include "ip4_text.h"
#include "settings.h"

// mbedTLS asks the platform for a millisecond clock (pwrman_mbedtls_config.h
// sets MBEDTLS_PLATFORM_MS_TIME_ALT); it only times handshake steps with it.
mbedtls_ms_time_t mbedtls_ms_time(void) {
    return (mbedtls_ms_time_t)to_ms_since_boot(get_absolute_time());
}

const char *mqtt_ca_check(const uint8_t *der, size_t len) {
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    int ret = mbedtls_x509_crt_parse_der(&crt, der, len);
    mbedtls_x509_crt_free(&crt);
    return ret == 0 ? NULL : "mqtt_ca: not an X.509 certificate";
}

const char *mqtt_tls_mode_str(void) {
    if (!g_settings.mqtt_tls) return "plain";
    return g_settings.mqtt_ca_len ? "verified" : "unverified";
}

bool mqtt_tls_wants_clock(void) {
    return g_settings.mqtt_tls && g_settings.mqtt_ca_len;
}

bool mqtt_ca_describe(char *out, size_t cap) {
    if (!g_settings.mqtt_ca_len) return false;
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    if (mbedtls_x509_crt_parse_der(&crt, g_settings.mqtt_ca, g_settings.mqtt_ca_len) != 0) {
        mbedtls_x509_crt_free(&crt);
        snprintf(out, cap, "the installed certificate does not parse");
        return true;
    }
    char subject[72];
    if (mbedtls_x509_dn_gets(subject, sizeof(subject), &crt.subject) < 0) strcpy(subject, "?");
    uint8_t fp[32];
    mbedtls_sha256(g_settings.mqtt_ca, g_settings.mqtt_ca_len, fp, 0);
    size_t o = (size_t)snprintf(out, cap, "%s; expires %04d-%02d-%02d; SHA-256 ", subject,
                                crt.valid_to.year, crt.valid_to.mon, crt.valid_to.day);
    for (int i = 0; i < 32 && o + 3 < cap; i++)
        o += (size_t)snprintf(out + o, cap - o, "%02X%s", fp[i], i < 31 ? ":" : "");
    mbedtls_x509_crt_free(&crt);
    return true;
}

// mbedTLS reports each certificate it refuses here before the handshake
// fails, so the console says why rather than just "disconnected".
static int verify_cb(void *arg, mbedtls_x509_crt *crt, int depth, uint32_t *flags) {
    (void)arg;
    (void)crt;
    if (!*flags) return 0;
    const char *why = *flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED ? "is not signed by the installed certificate"
                    : *flags & MBEDTLS_X509_BADCERT_CN_MISMATCH ? "does not carry the broker's name"
                    : *flags & MBEDTLS_X509_BADCERT_EXPIRED     ? "has expired"
                    : *flags & MBEDTLS_X509_BADCERT_FUTURE      ? "is not valid yet"
                                                                 : "was refused";
    printf("mqtt: broker certificate (depth %d) %s (flags 0x%lx)\n", depth, why, (unsigned long)*flags);
    return 0;
}

static struct altcp_tls_config *conf, *retired;

struct altcp_tls_config *mqtt_tls_config(void) {
    // The configuration before last goes now, not the last one: a connection
    // that is still closing may hold a pointer to that.
    if (retired) altcp_tls_free_config(retired);
    retired = conf;
    conf = g_settings.mqtt_ca_len
               ? altcp_tls_create_config_client(g_settings.mqtt_ca, g_settings.mqtt_ca_len)
               : altcp_tls_create_config_client(NULL, 0);
    if (!conf) return NULL;
    // lwIP's glue keeps the mbedTLS configuration as the first member of the
    // struct it hands back and defines that struct only in its own source
    // file, so this cast is how the verification mode is reached. The
    // compiled-in mode is "required" (lwipopts.h); the unverified state
    // relaxes it, the verified state adds the reason log above.
    mbedtls_ssl_config *ssl_conf = (mbedtls_ssl_config *)conf;
    if (g_settings.mqtt_ca_len) mbedtls_ssl_conf_verify(ssl_conf, verify_cb, NULL);
    else mbedtls_ssl_conf_authmode(ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
    return conf;
}

void mqtt_tls_set_hostname(struct mqtt_client_s *client) {
    uint32_t addr;
    if (!client->conn || ip4_parse(g_settings.mqtt_host, &addr)) return;
    mbedtls_ssl_context *ssl = (mbedtls_ssl_context *)altcp_tls_context(client->conn);
    if (ssl) mbedtls_ssl_set_hostname(ssl, g_settings.mqtt_host);
}
