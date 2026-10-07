#include <string.h>

#include "http_req.h"
#include "microtest.h"

#define REQ(headers, body) "POST /api/v1/settings HTTP/1.1\r\n" headers "\r\n" body

static void test_header_lookup(void) {
    char v[32];
    const char *r = REQ("host: 10.0.0.5\r\nCONTENT-TYPE:  text/plain  \r\n", "{}");
    MT_ASSERT(http_req_header(r, "Host", v, sizeof(v)));
    MT_ASSERT(!strcmp(v, "10.0.0.5"));
    MT_ASSERT(http_req_header(r, "Content-Type", v, sizeof(v)));
    MT_ASSERT(!strcmp(v, "text/plain"));
    MT_ASSERT(!http_req_header(r, "Origin", v, sizeof(v)));
    // a longer name with the same prefix is a different header
    MT_ASSERT(!http_req_header(REQ("Hostname: x\r\n", ""), "Host", v, sizeof(v)));
    // a value that does not fit is refused, not cut short
    MT_ASSERT(!http_req_header(r, "Host", v, 8));
}

// Header lines inside the body are not headers.
static void test_body_is_not_headers(void) {
    char v[32];
    const char *r = REQ("Content-Type: text/plain\r\n", "x\r\nHost: pwrman\r\nContent-Type: application/json\r\n");
    MT_ASSERT(!http_req_header(r, "Host", v, sizeof(v)));
    MT_ASSERT(!http_req_is_json(r));
}

static void test_json_content_type(void) {
    MT_ASSERT(http_req_is_json(REQ("Content-Type: application/json\r\n", "{}")));
    MT_ASSERT(http_req_is_json(REQ("content-type: Application/JSON; charset=utf-8\r\n", "{}")));
    // what a form, a no-cors fetch or a beacon can send from another site
    MT_ASSERT(!http_req_is_json(REQ("Content-Type: text/plain;charset=UTF-8\r\n", "{}")));
    MT_ASSERT(!http_req_is_json(REQ("Content-Type: application/x-www-form-urlencoded\r\n", "{}")));
    MT_ASSERT(!http_req_is_json(REQ("Content-Type: multipart/form-data; boundary=x\r\n", "{}")));
    MT_ASSERT(!http_req_is_json(REQ("Content-Type: application/jsonx\r\n", "{}")));
    MT_ASSERT(!http_req_is_json(REQ("", "{}")));
}

static void test_host_names_the_controller(void) {
    const char *ip = "192.168.1.50", *name = "pwrman";
    MT_ASSERT(http_req_host_ok(REQ("Host: 192.168.1.50\r\n", ""), 80, ip, name, "", ""));
    MT_ASSERT(http_req_host_ok(REQ("Host: 192.168.1.50:80\r\n", ""), 80, ip, name, "", ""));
    MT_ASSERT(http_req_host_ok(REQ("Host: pwrman\r\n", ""), 80, ip, name, "", ""));
    MT_ASSERT(http_req_host_ok(REQ("Host: PwrMan.local\r\n", ""), 80, ip, name, "", ""));
    MT_ASSERT(http_req_host_ok(REQ("Host: pwrman.local.\r\n", ""), 80, ip, name, "", ""));
}

// The owner's own names for the controller: a LAN DNS record, a tailnet
// name, a proxy that passes Host through
static void test_host_extra_names(void) {
    const char *ip = "192.168.1.50", *name = "pwrman", *list = "pm.home.lan pm.tail1234.ts.net";
    MT_ASSERT(http_req_host_ok(REQ("Host: pm.home.lan\r\n", ""), 80, ip, name, list, ""));
    MT_ASSERT(http_req_host_ok(REQ("Host: PM.Tail1234.ts.net.:80\r\n", ""), 80, ip, name, list, ""));
    // the address and the device name stay allowed whatever the list holds
    MT_ASSERT(http_req_host_ok(REQ("Host: 192.168.1.50\r\n", ""), 80, ip, name, list, ""));
    MT_ASSERT(http_req_host_ok(REQ("Host: pwrman.local\r\n", ""), 80, ip, name, list, ""));
    MT_ASSERT(!http_req_host_ok(REQ("Host: pm.home\r\n", ""), 80, ip, name, list, ""));
    MT_ASSERT(!http_req_host_ok(REQ("Host: home.lan\r\n", ""), 80, ip, name, list, ""));
    MT_ASSERT(!http_req_host_ok(REQ("Host: x.pm.home.lan\r\n", ""), 80, ip, name, list, ""));
}

// A port-443 request, and the names the HTTPS certificate covers
static void test_host_https(void) {
    const char *ip = "192.168.1.50", *name = "pwrman", *cert = "pm.home.example.com *.lab.example.com";
    MT_ASSERT(http_req_host_ok(REQ("Host: pm.home.example.com\r\n", ""), 443, ip, name, "", cert));
    MT_ASSERT(http_req_host_ok(REQ("Host: PM.Home.Example.com:443\r\n", ""), 443, ip, name, "", cert));
    MT_ASSERT(http_req_host_ok(REQ("Host: pwrman.local:443\r\n", ""), 443, ip, name, "", cert));
    MT_ASSERT(!http_req_host_ok(REQ("Host: pwrman.local:80\r\n", ""), 443, ip, name, "", cert));
    MT_ASSERT(!http_req_host_ok(REQ("Host: pwrman.local:443\r\n", ""), 80, ip, name, "", cert));
    // a wildcard covers one label, no more and no fewer
    MT_ASSERT(http_req_host_ok(REQ("Host: pm.lab.example.com\r\n", ""), 443, ip, name, "", cert));
    MT_ASSERT(!http_req_host_ok(REQ("Host: lab.example.com\r\n", ""), 443, ip, name, "", cert));
    MT_ASSERT(!http_req_host_ok(REQ("Host: a.pm.lab.example.com\r\n", ""), 443, ip, name, "", cert));
    MT_ASSERT(!http_req_host_ok(REQ("Host: home.example.com\r\n", ""), 443, ip, name, "", cert));
    // certificate names count on port 80 too: its redirect names them
    MT_ASSERT(http_req_host_ok(REQ("Host: pm.home.example.com\r\n", ""), 80, ip, name, "", cert));
}

static void test_keep_alive(void) {
    MT_ASSERT(http_req_keep_alive("GET / HTTP/1.1\r\nHost: x\r\n\r\n"));
    MT_ASSERT(!http_req_keep_alive("GET / HTTP/1.1\r\nConnection: close\r\n\r\n"));
    MT_ASSERT(!http_req_keep_alive("GET / HTTP/1.1\r\nconnection: Upgrade, Close\r\n\r\n"));
    MT_ASSERT(!http_req_keep_alive("GET / HTTP/1.0\r\n\r\n"));
    MT_ASSERT(http_req_keep_alive("GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n"));
    MT_ASSERT(!http_req_keep_alive("GET /\r\n\r\n"));
    // a header in the body does not count
    MT_ASSERT(http_req_keep_alive("POST /x HTTP/1.1\r\nHost: x\r\n\r\nConnection: close\r\n"));
}

static void test_https_location(void) {
    char out[96];
    MT_ASSERT(http_req_https_location("GET /api/v1/status?x=1 HTTP/1.1\r\nHost: pm.example.com:80\r\n\r\n",
                                      out, sizeof(out)));
    MT_ASSERT(!strcmp(out, "https://pm.example.com/api/v1/status?x=1"));
    MT_ASSERT(http_req_https_location("GET / HTTP/1.1\r\nHost: 192.168.1.50\r\n\r\n", out, sizeof(out)));
    MT_ASSERT(!strcmp(out, "https://192.168.1.50/"));
    MT_ASSERT(!http_req_https_location("GET / HTTP/1.1\r\n\r\n", out, sizeof(out)));
    MT_ASSERT(!http_req_https_location("GET * HTTP/1.1\r\nHost: a\r\n\r\n", out, sizeof(out)));
    MT_ASSERT(!http_req_https_location("GET / HTTP/1.1\r\nHost: a\r\n\r\n", out, 10));
}

static void test_hostnames_parse(void) {
    char out[64];
    MT_ASSERT(http_req_hostnames_parse(" PM.Home.LAN, pm.tail1234.ts.net. ,,", out, sizeof(out)) == NULL);
    MT_ASSERT(!strcmp(out, "pm.home.lan pm.tail1234.ts.net"));
    MT_ASSERT(http_req_hostnames_parse("", out, sizeof(out)) == NULL);
    MT_ASSERT(!strcmp(out, ""));
    MT_ASSERT(http_req_hostnames_parse("pm.home.lan:8080", out, sizeof(out)) != NULL);
    MT_ASSERT(http_req_hostnames_parse("pm_home", out, sizeof(out)) != NULL);
    MT_ASSERT(http_req_hostnames_parse("pm..lan", out, sizeof(out)) != NULL);
    MT_ASSERT(http_req_hostnames_parse(".lan", out, sizeof(out)) != NULL);
    MT_ASSERT(http_req_hostnames_parse("-pm.lan", out, sizeof(out)) != NULL);
    MT_ASSERT(http_req_hostnames_parse("pm.-x", out, sizeof(out)) != NULL);
    MT_ASSERT(http_req_hostnames_parse("http://pm.lan", out, sizeof(out)) != NULL);
    // a list that does not fit is refused whole
    MT_ASSERT(http_req_hostnames_parse("aaaaaaaaaaaaaaaaaaaa bbbbbbbbbbbbbbbbbbbb cccccccccccccccccccc dddd",
                                       out, sizeof(out)) != NULL);
}

// A rebinding page's requests name its own domain.
static void test_host_refuses_other_names(void) {
    const char *ip = "192.168.1.50", *name = "pwrman";
    MT_ASSERT(!http_req_host_ok(REQ("Host: evil.example\r\n", ""), 80, ip, name, "", ""));
    MT_ASSERT(!http_req_host_ok(REQ("Host: pwrman.evil.example\r\n", ""), 80, ip, name, "", ""));
    MT_ASSERT(!http_req_host_ok(REQ("Host: pwrmanx.local\r\n", ""), 80, ip, name, "", ""));
    MT_ASSERT(!http_req_host_ok(REQ("Host: 192.168.1.5\r\n", ""), 80, ip, name, "", ""));
    MT_ASSERT(!http_req_host_ok(REQ("Host: 192.168.1.50:8080\r\n", ""), 80, ip, name, "", ""));
    MT_ASSERT(!http_req_host_ok(REQ("Host: \r\n", ""), 80, ip, name, "", ""));
    MT_ASSERT(!http_req_host_ok(REQ("", ""), 80, ip, name, "", ""));
    // an empty device name never matches a bare .local
    MT_ASSERT(!http_req_host_ok(REQ("Host: .local\r\n", ""), 80, ip, "", "", ""));
}

static void test_bearer(void) {
    MT_ASSERT(http_req_bearer_ok(REQ("Authorization: Bearer s3cret\r\n", ""), "s3cret"));
    MT_ASSERT(http_req_bearer_ok(REQ("authorization: bearer s3cret\r\n", ""), "s3cret"));
    MT_ASSERT(!http_req_bearer_ok(REQ("Authorization: Bearer s3creT\r\n", ""), "s3cret"));
    MT_ASSERT(!http_req_bearer_ok(REQ("Authorization: Bearer s3cre\r\n", ""), "s3cret"));   // a prefix
    MT_ASSERT(!http_req_bearer_ok(REQ("Authorization: Bearer s3crets\r\n", ""), "s3cret")); // longer
    MT_ASSERT(!http_req_bearer_ok(REQ("Authorization: Bearer \r\n", ""), "s3cret"));
    MT_ASSERT(!http_req_bearer_ok(REQ("Authorization: Basic s3cret\r\n", ""), "s3cret"));
    MT_ASSERT(!http_req_bearer_ok(REQ("", "Authorization: Bearer s3cret\r\n"), "s3cret")); // in the body
    // no secret stored never matches, even an empty bearer
    MT_ASSERT(!http_req_bearer_ok(REQ("Authorization: Bearer \r\n", ""), ""));
}

static void test_line_text(void) {
    char out[24];
    http_req_line_text("POST /api/v1/settings?secrets=1 HTTP/1.1\r\nHost: x\r\n\r\n", out, sizeof(out));
    MT_ASSERT(!strcmp(out, "POST /api/v1/settings"));
    http_req_line_text("GET /a\x1b[2Jb HTTP/1.1\r\n", out, sizeof(out));
    MT_ASSERT(!strcmp(out, "GET /a?[2Jb"));
    http_req_line_text("GET /a-very-long-path-that-goes-on HTTP/1.1\r\n", out, sizeof(out));
    MT_ASSERT(strlen(out) == sizeof(out) - 1);
    http_req_line_text("", out, sizeof(out));
    MT_ASSERT(!strcmp(out, ""));
}

static void test_refusal_rate_limit(void) {
    http_refusal_limit_t l = {0};
    unsigned unlogged = 99;
    for (unsigned i = 0; i < HTTP_REFUSAL_LOG_MAX; i++) {
        MT_ASSERT(http_refusal_log_ok(&l, 1000 + i, &unlogged));
        MT_ASSERT_EQ(unlogged, 0);
    }
    for (unsigned i = 0; i < 40; i++) MT_ASSERT(!http_refusal_log_ok(&l, 2000 + i, &unlogged));
    // the next minute logs again and reports what was held back
    MT_ASSERT(http_refusal_log_ok(&l, 1000 + HTTP_REFUSAL_WINDOW_MS, &unlogged));
    MT_ASSERT_EQ(unlogged, 40);
    MT_ASSERT(http_refusal_log_ok(&l, 1001 + HTTP_REFUSAL_WINDOW_MS, &unlogged));
    MT_ASSERT_EQ(unlogged, 0);
}

void run_http_req_tests(void) {
    mt_run("http_req: header lookup", test_header_lookup);
    mt_run("http_req: header lines in the body are ignored", test_body_is_not_headers);
    mt_run("http_req: only application/json counts as JSON", test_json_content_type);
    mt_run("http_req: Host may name the controller", test_host_names_the_controller);
    mt_run("http_req: Host naming anything else is refused", test_host_refuses_other_names);
    mt_run("http_req: the owner's extra names are allowed", test_host_extra_names);
    mt_run("http_req: HTTPS ports and certificate names", test_host_https);
    mt_run("http_req: keep-alive follows the HTTP version and Connection", test_keep_alive);
    mt_run("http_req: the HTTPS redirect target", test_https_location);
    mt_run("http_req: the hostnames setting is checked and tidied", test_hostnames_parse);
    mt_run("http_req: the bearer must match the secret exactly", test_bearer);
    mt_run("http_req: the request line is cleaned for the log", test_line_text);
    mt_run("http_req: refusal lines are rate-limited", test_refusal_rate_limit);
}
