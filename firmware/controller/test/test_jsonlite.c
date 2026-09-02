#include <string.h>

#include "jsonlite.h"
#include "microtest.h"

// jsonlite: the flat-object extractor behind the settings API. Bodies come
// from JSON.stringify in the page or from curl, so the cases that matter are
// escapes in passwords, keys that appear inside other values, and size caps.

static void test_get_str_plain(void) {
    char out[32];
    MT_ASSERT_EQ(json_get_str("{\"name\":\"pwrman\",\"mqtt_port\":1883}", "name", out, sizeof(out)), 1);
    MT_ASSERT(!strcmp(out, "pwrman"));
    MT_ASSERT_EQ(json_get_str("{ \"name\" : \"a b\" }", "name", out, sizeof(out)), 1);
    MT_ASSERT(!strcmp(out, "a b"));
    MT_ASSERT_EQ(json_get_str("{\"name\":\"\"}", "name", out, sizeof(out)), 1);
    MT_ASSERT(!strcmp(out, ""));
}

static void test_get_str_absent_or_not_string(void) {
    char out[32] = "untouched";
    MT_ASSERT_EQ(json_get_str("{\"mqtt_host\":\"h\"}", "name", out, sizeof(out)), 0);
    MT_ASSERT_EQ(json_get_str("{\"name\":42}", "name", out, sizeof(out)), 0);
    MT_ASSERT_EQ(json_get_str("{\"name\":null}", "name", out, sizeof(out)), 0);
    MT_ASSERT(!strcmp(out, "untouched"));
    // unterminated string, malformed escape
    MT_ASSERT_EQ(json_get_str("{\"name\":\"abc", "name", out, sizeof(out)), 0);
    MT_ASSERT_EQ(json_get_str("{\"name\":\"a\\qb\"}", "name", out, sizeof(out)), 0);
    MT_ASSERT_EQ(json_get_str("{\"name\":\"a\\u12\"}", "name", out, sizeof(out)), 0);
}

static void test_get_str_key_inside_value_is_skipped(void) {
    char out[32];
    // the text "name" appears first as a value, then as the real key
    MT_ASSERT_EQ(json_get_str("{\"x\":\"name\",\"name\":\"real\"}", "name", out, sizeof(out)), 1);
    MT_ASSERT(!strcmp(out, "real"));
    // a key that merely starts with the wanted key
    MT_ASSERT_EQ(json_get_str("{\"names\":\"no\",\"name\":\"yes\"}", "name", out, sizeof(out)), 1);
    MT_ASSERT(!strcmp(out, "yes"));
    // the key text with no colon after it is not a key
    MT_ASSERT_EQ(json_get_str("{\"k\":\"name\"}", "name", out, sizeof(out)), 0);
}

static void test_get_str_escapes(void) {
    char out[32];
    MT_ASSERT_EQ(json_get_str("{\"p\":\"a\\\"b\\\\c\\/d\\n\\t\"}", "p", out, sizeof(out)), 1);
    MT_ASSERT(!strcmp(out, "a\"b\\c/d\n\t"));
    MT_ASSERT_EQ(json_get_str("{\"p\":\"\\u0041\\u00e9\\u20ac\"}", "p", out, sizeof(out)), 1);
    MT_ASSERT(!strcmp(out, "A\xc3\xa9\xe2\x82\xac"));
    // raw UTF-8 passes through unchanged
    MT_ASSERT_EQ(json_get_str("{\"p\":\"caf\xc3\xa9\"}", "p", out, sizeof(out)), 1);
    MT_ASSERT(!strcmp(out, "caf\xc3\xa9"));
    // surrogates are not supported: replaced, not garbled
    MT_ASSERT_EQ(json_get_str("{\"p\":\"\\ud83d\\ude00!\"}", "p", out, sizeof(out)), 1);
    MT_ASSERT(!strcmp(out, "?" "?!"));
}

static void test_get_str_too_long(void) {
    char out[5];
    MT_ASSERT_EQ(json_get_str("{\"p\":\"abcdefgh\",\"q\":\"z\"}", "p", out, sizeof(out)), -1);
    MT_ASSERT(!strcmp(out, "abcd"));
    // exactly fits
    MT_ASSERT_EQ(json_get_str("{\"p\":\"abcd\"}", "p", out, sizeof(out)), 1);
    MT_ASSERT(!strcmp(out, "abcd"));
    // a multi-byte escape that does not fit is dropped whole
    MT_ASSERT_EQ(json_get_str("{\"p\":\"abc\\u20ac\"}", "p", out, sizeof(out)), -1);
    MT_ASSERT(!strcmp(out, "abc"));
    // still finds later keys after a truncated one
    MT_ASSERT_EQ(json_get_str("{\"p\":\"abcdefgh\",\"q\":\"z\"}", "q", out, sizeof(out)), 1);
    MT_ASSERT(!strcmp(out, "z"));
}

static void test_get_int(void) {
    long v = -1;
    MT_ASSERT(json_get_int("{\"mqtt_host\":\"h\",\"mqtt_port\":1883}", "mqtt_port", &v));
    MT_ASSERT_EQ(v, 1883);
    MT_ASSERT(json_get_int("{ \"watts\" : -5 }", "watts", &v));
    MT_ASSERT_EQ(v, -5);
    MT_ASSERT(!json_get_int("{\"watts\":\"5\"}", "watts", &v)); // quoted: not a number
    MT_ASSERT(!json_get_int("{\"watts\":true}", "watts", &v));
    MT_ASSERT(!json_get_int("{\"w\":5}", "watts", &v));
    MT_ASSERT_EQ(v, -5); // untouched on failure
}

static void test_escape(void) {
    char out[32];
    MT_ASSERT_EQ(json_escape(out, sizeof(out), "a\"b\\c\x01"), 13);
    MT_ASSERT(!strcmp(out, "a\\\"b\\\\c\\u0001"));
    MT_ASSERT_EQ(json_escape(out, sizeof(out), "caf\xc3\xa9/"), 6);
    MT_ASSERT(!strcmp(out, "caf\xc3\xa9/"));
    // truncation never leaves half an escape
    MT_ASSERT_EQ(json_escape(out, 3, "a\""), 1);
    MT_ASSERT(!strcmp(out, "a"));
    MT_ASSERT_EQ(json_escape(out, 1, "abc"), 0);
    MT_ASSERT(!strcmp(out, ""));
}

void run_jsonlite_tests(void) {
    mt_run("jsonlite: plain strings", test_get_str_plain);
    mt_run("jsonlite: absent / non-string values", test_get_str_absent_or_not_string);
    mt_run("jsonlite: key text inside a value", test_get_str_key_inside_value_is_skipped);
    mt_run("jsonlite: escapes and UTF-8", test_get_str_escapes);
    mt_run("jsonlite: capped output", test_get_str_too_long);
    mt_run("jsonlite: integers", test_get_int);
    mt_run("jsonlite: escaping for output", test_escape);
}
