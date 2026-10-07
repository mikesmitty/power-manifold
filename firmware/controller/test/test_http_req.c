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
    MT_ASSERT(http_req_host_ok(REQ("Host: 192.168.1.50\r\n", ""), ip, name, ""));
    MT_ASSERT(http_req_host_ok(REQ("Host: 192.168.1.50:80\r\n", ""), ip, name, ""));
    MT_ASSERT(http_req_host_ok(REQ("Host: pwrman\r\n", ""), ip, name, ""));
    MT_ASSERT(http_req_host_ok(REQ("Host: PwrMan.local\r\n", ""), ip, name, ""));
    MT_ASSERT(http_req_host_ok(REQ("Host: pwrman.local.\r\n", ""), ip, name, ""));
}

// The owner's own names for the controller: a LAN DNS record, a tailnet
// name, a proxy that passes Host through
static void test_host_extra_names(void) {
    const char *ip = "192.168.1.50", *name = "pwrman", *list = "pm.home.lan pm.tail1234.ts.net";
    MT_ASSERT(http_req_host_ok(REQ("Host: pm.home.lan\r\n", ""), ip, name, list));
    MT_ASSERT(http_req_host_ok(REQ("Host: PM.Tail1234.ts.net.:80\r\n", ""), ip, name, list));
    // the address and the device name stay allowed whatever the list holds
    MT_ASSERT(http_req_host_ok(REQ("Host: 192.168.1.50\r\n", ""), ip, name, list));
    MT_ASSERT(http_req_host_ok(REQ("Host: pwrman.local\r\n", ""), ip, name, list));
    MT_ASSERT(!http_req_host_ok(REQ("Host: pm.home\r\n", ""), ip, name, list));
    MT_ASSERT(!http_req_host_ok(REQ("Host: home.lan\r\n", ""), ip, name, list));
    MT_ASSERT(!http_req_host_ok(REQ("Host: x.pm.home.lan\r\n", ""), ip, name, list));
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
    MT_ASSERT(!http_req_host_ok(REQ("Host: evil.example\r\n", ""), ip, name, ""));
    MT_ASSERT(!http_req_host_ok(REQ("Host: pwrman.evil.example\r\n", ""), ip, name, ""));
    MT_ASSERT(!http_req_host_ok(REQ("Host: pwrmanx.local\r\n", ""), ip, name, ""));
    MT_ASSERT(!http_req_host_ok(REQ("Host: 192.168.1.5\r\n", ""), ip, name, ""));
    MT_ASSERT(!http_req_host_ok(REQ("Host: 192.168.1.50:8080\r\n", ""), ip, name, ""));
    MT_ASSERT(!http_req_host_ok(REQ("Host: \r\n", ""), ip, name, ""));
    MT_ASSERT(!http_req_host_ok(REQ("", ""), ip, name, ""));
    // an empty device name never matches a bare .local
    MT_ASSERT(!http_req_host_ok(REQ("Host: .local\r\n", ""), ip, "", ""));
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

void run_http_req_tests(void) {
    mt_run("http_req: header lookup", test_header_lookup);
    mt_run("http_req: header lines in the body are ignored", test_body_is_not_headers);
    mt_run("http_req: only application/json counts as JSON", test_json_content_type);
    mt_run("http_req: Host may name the controller", test_host_names_the_controller);
    mt_run("http_req: Host naming anything else is refused", test_host_refuses_other_names);
    mt_run("http_req: the owner's extra names are allowed", test_host_extra_names);
    mt_run("http_req: the hostnames setting is checked and tidied", test_hostnames_parse);
    mt_run("http_req: the bearer must match the secret exactly", test_bearer);
}
