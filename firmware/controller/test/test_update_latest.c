#include <string.h>

#include "microtest.h"
#include "net/http_url.h"
#include "update_latest.h"

#define URL_11 "http://fw.powermanifold.io/controller/0.11.0/pwrman_controller_card/controller.signed.bin"
#define URL_12 "http://fw.powermanifold.io/controller/0.12.0/pwrman_controller_card/controller.signed.bin"

static void test_url_parts(void) {
    http_url_t u;
    char e[64];
    MT_ASSERT(http_url_parse("http://fw.powermanifold.io/controller/pwrman_controller_card/latest.json", &u, e, sizeof(e)));
    MT_ASSERT(strcmp(u.host, "fw.powermanifold.io") == 0);
    MT_ASSERT_EQ(u.port, 80);
    MT_ASSERT(strcmp(u.path, "/controller/pwrman_controller_card/latest.json") == 0);
    MT_ASSERT(http_url_parse("http://10.0.0.5:8000", &u, e, sizeof(e)));
    MT_ASSERT(strcmp(u.host, "10.0.0.5") == 0);
    MT_ASSERT_EQ(u.port, 8000);
    MT_ASSERT(strcmp(u.path, "/") == 0);
    MT_ASSERT(http_url_parse("http://host/a:b", &u, e, sizeof(e))); // a colon in the path is not a port
    MT_ASSERT_EQ(u.port, 80);
}

static void test_url_refusals(void) {
    http_url_t u;
    char e[64];
    MT_ASSERT(!http_url_parse("https://fw.powermanifold.io/x", &u, e, sizeof(e))); // no TLS on this stack
    MT_ASSERT(strstr(e, "https") != NULL);
    MT_ASSERT(!http_url_parse("fw.powermanifold.io/x", &u, e, sizeof(e)));
    MT_ASSERT(!http_url_parse("http:///x", &u, e, sizeof(e)));
    MT_ASSERT(!http_url_parse("http://host:0/x", &u, e, sizeof(e)));
    MT_ASSERT(!http_url_parse("http://host:70000/x", &u, e, sizeof(e)));
    char longhost[7 + HTTP_URL_HOST_MAX + 2] = "http://";
    memset(longhost + 7, 'a', HTTP_URL_HOST_MAX);
    MT_ASSERT(!http_url_parse(longhost, &u, e, sizeof(e)));
    char longpath[7 + 2 + HTTP_URL_PATH_MAX + 2] = "http://h/";
    memset(longpath + 9, 'p', HTTP_URL_PATH_MAX);
    MT_ASSERT(!http_url_parse(longpath, &u, NULL, 0)); // and no message buffer is fine
}

static void test_first_pointer_is_taken(void) {
    update_latest_clear();
    unsigned seq = update_latest_seq();
    MT_ASSERT(update_latest_version()[0] == '\0');
    MT_ASSERT(!update_latest_newer_than("0.10.0"));
    MT_ASSERT(update_latest_offer_json("{\"version\":\"0.11.0\",\"url\":\"" URL_11 "\"}"));
    MT_ASSERT(strcmp(update_latest_version(), "0.11.0") == 0);
    MT_ASSERT(strcmp(update_latest_url(), URL_11) == 0);
    MT_ASSERT(update_latest_seq() != seq);
    MT_ASSERT(update_latest_newer_than("0.10.0"));
    MT_ASSERT(!update_latest_newer_than("0.11.0")); // running it already
    MT_ASSERT(!update_latest_newer_than("0.12.0"));
}

// The check and the MQTT pointer both feed this: the newer release wins,
// whichever arrives first, and an older or equal one changes nothing.
static void test_newer_pointer_wins(void) {
    update_latest_clear();
    MT_ASSERT(update_latest_offer("0.12.0", URL_12));
    unsigned seq = update_latest_seq();
    MT_ASSERT(!update_latest_offer("0.11.0", URL_11));
    MT_ASSERT(!update_latest_offer("0.12.0", "http://lan-box/controller.signed.bin"));
    MT_ASSERT(strcmp(update_latest_url(), URL_12) == 0);
    MT_ASSERT_EQ(update_latest_seq(), seq);
    MT_ASSERT(update_latest_offer("0.12.1", "http://lan-box/controller.signed.bin"));
    MT_ASSERT(strcmp(update_latest_version(), "0.12.1") == 0);
    MT_ASSERT(update_latest_offer("0.100.0", URL_12)); // 100 > 12: compared as numbers
    MT_ASSERT(strcmp(update_latest_version(), "0.100.0") == 0);
}

static void test_malformed_pointers(void) {
    update_latest_clear();
    MT_ASSERT(!update_latest_offer_json(""));
    MT_ASSERT(!update_latest_offer_json("<html>404</html>"));
    MT_ASSERT(!update_latest_offer_json("{\"version\":\"0.12.0\"}"));
    MT_ASSERT(!update_latest_offer_json("{\"url\":\"" URL_12 "\"}"));
    MT_ASSERT(!update_latest_offer_json("{\"version\":\"latest\",\"url\":\"" URL_12 "\"}"));
    MT_ASSERT(!update_latest_offer_json("{\"version\":\"0.12.0\",\"url\":\"https://github.com/x\"}"));
    MT_ASSERT(!update_latest_offer_json("{\"version\":\"0.12.0\",\"url\":\"ftp://host/x\"}"));
    MT_ASSERT(!update_latest_offer_json("{\"version\":\"0.12.0-rc1-and-much-more\",\"url\":\"" URL_12 "\"}"));
    MT_ASSERT(update_latest_version()[0] == '\0');
}

static void test_source_setting(void) {
    MT_ASSERT(update_source_valid("", 64)); // never ask
    MT_ASSERT(update_source_valid(UPDATE_SOURCE_DEFAULT, 64));
    MT_ASSERT(update_source_valid("http://10.0.0.5:8000/fw", 64));
    MT_ASSERT(!update_source_valid("http://fw.powermanifold.io/", 64)); // the path is appended
    MT_ASSERT(!update_source_valid("https://fw.powermanifold.io", 64));
    MT_ASSERT(!update_source_valid("fw.powermanifold.io", 64));
    MT_ASSERT(!update_source_valid("http://fw power", 64));
    MT_ASSERT(!update_source_valid("http://a-host-name-that-is-far-too-long-to-store.example.com/firmware", 64));

    char url[128];
    MT_ASSERT(update_source_pointer_url(UPDATE_SOURCE_DEFAULT, "pwrman_controller_card", url, sizeof(url)));
    MT_ASSERT(strcmp(url, "http://fw.powermanifold.io/controller/pwrman_controller_card/latest.json") == 0);
    MT_ASSERT(!update_source_pointer_url(UPDATE_SOURCE_DEFAULT, "pwrman_controller_card", url, 40));
}

void run_update_latest_tests(void) {
    mt_run("url: host, port and path", test_url_parts);
    mt_run("url: what is refused", test_url_refusals);
    mt_run("latest: the first pointer is taken", test_first_pointer_is_taken);
    mt_run("latest: only a newer release replaces it", test_newer_pointer_wins);
    mt_run("latest: malformed pointers are ignored", test_malformed_pointers);
    mt_run("latest: the update source setting", test_source_setting);
}
