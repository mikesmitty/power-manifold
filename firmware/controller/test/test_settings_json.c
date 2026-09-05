#include <string.h>

#include "ip4_text.h"
#include "manifold.h"
#include "microtest.h"
#include "settings.h"
#include "settings_json.h"

static char json[2560];

static uint32_t ip(const char *s) {
    uint32_t a = 0;
    ip4_parse(s, &a); // literals below are valid; test_ip4_text covers the parser
    return a;
}

// a fully populated record, nothing at its default
static void fill(settings_t *s) {
    memset(s, 0, sizeof(*s));
    strcpy(s->device_name, "bench-2");
    strcpy(s->wifi_ssid, "lab \"net\"");
    strcpy(s->wifi_pass, "hunter\\2");
    strcpy(s->mqtt_host, "broker.example");
    s->mqtt_port = 8883;
    strcpy(s->mqtt_user, "pwr");
    strcpy(s->mqtt_pass, "s3cret");
    strcpy(s->api_token, "tok-123");
    s->budget_mw = 250000;
    s->fan_auto = 0;
    s->fan_on_w = 90;
    s->fan_off_w = 70;
    s->fan_on_ma = 2500;
    s->led_brightness = 12;
    s->led_boot = LED_BOOT_RAINBOW;
    s->ip_static = 1;
    s->ip_addr = ip("10.100.55.202");
    s->ip_mask = ip("255.255.255.0");
    s->ip_gw = ip("10.100.55.1");
    s->ip_dns = ip("10.64.0.2");
    strcpy(s->syslog_host, "logs.example");
    s->syslog_port = 5514;
    s->charged_mw = 750;
    s->charged_min = 20;
    s->port_auto_off = 0x15;
    s->led_dim = 3;
    s->led_night_start = 22 * 60;
    s->led_night_end = 6 * 60 + 30;
    s->led_idle_min = 45;
    s->tz_offset_min = -240;
    for (int i = 0; i < NUM_PORTS; i++) {
        snprintf(s->port_name[i], sizeof(s->port_name[i]), "Slot %d <b>&", i + 1);
        s->port_limit_ma[i] = 1000u + 500u * (uint32_t)i;
        s->port_boot[i] = (uint8_t)(i % 3);
        s->port_priority[i] = (uint8_t)(5 - i);
        s->port_sleep_min[i] = (uint16_t)(i * 90);
    }
    s->port_off_mask = 0x2A; // runtime bookkeeping: not part of the JSON
}

static void test_round_trip(void) {
    settings_t src, dst;
    fill(&src);
    settings_json_opts_t o = {.fan_on = true, .secrets = true, .export = true};
    size_t n = settings_json_build(json, sizeof(json), &src, &o);
    MT_ASSERT(n > 0);
    MT_ASSERT(strstr(json, "\"format\":1") != NULL);
    MT_ASSERT(strstr(json, "\"wifi_ssid\":\"lab \\\"net\\\"\"") != NULL);
    MT_ASSERT(strstr(json, "\"wifi_pass\":\"hunter\\\\2\"") != NULL);
    MT_ASSERT(strstr(json, "\"fan_mode\":\"on\"") != NULL); // manual, and the fan is on
    MT_ASSERT(strstr(json, "\"ip\":\"10.100.55.202\"") != NULL);
    MT_ASSERT(strstr(json, "\"port_priorities\":[5,4,3,2,1,0]") != NULL);
    MT_ASSERT(strstr(json, "\"port_auto_off\":[1,0,1,0,1,0]") != NULL);
    MT_ASSERT(strstr(json, "\"led_night\":\"22:00-06:30\"") != NULL);
    MT_ASSERT(strstr(json, "\"tz_offset_min\":-240") != NULL);
    MT_ASSERT(strstr(json, "\"port_sleep_min\":[0,90,180,270,360,450]") != NULL);

    memset(&dst, 0, sizeof(dst)); // a blank box importing the export
    settings_apply_t ap;
    const char *err = settings_json_apply(json, &dst, false, &ap);
    MT_ASSERT(err == NULL);
    MT_ASSERT(ap.fan_mode_given && ap.fan_manual_on);
    // compare field by field: the record's on-flash header and the
    // runtime off-mask are not carried by the JSON
    dst.port_off_mask = src.port_off_mask;
    MT_ASSERT(!memcmp(&src.wifi_ssid, &dst.wifi_ssid,
                      offsetof(settings_t, crc) - offsetof(settings_t, wifi_ssid)));
}

static void test_secrets_off_by_default(void) {
    settings_t src;
    fill(&src);
    settings_json_opts_t o = {0};
    MT_ASSERT(settings_json_build(json, sizeof(json), &src, &o) > 0);
    MT_ASSERT(strstr(json, "hunter") == NULL);
    MT_ASSERT(strstr(json, "s3cret") == NULL);
    MT_ASSERT(strstr(json, "tok-123") == NULL);
    MT_ASSERT(strstr(json, "\"token_set\":true") != NULL);
    MT_ASSERT(strstr(json, "\"format\"") == NULL);
    // ...and importing such an export keeps the secrets the box already has
    settings_t dst = src;
    strcpy(dst.wifi_pass, "keep-me");
    settings_apply_t ap;
    MT_ASSERT(settings_json_apply(json, &dst, false, &ap) == NULL);
    MT_ASSERT(!strcmp(dst.wifi_pass, "keep-me"));
    MT_ASSERT(!strcmp(dst.api_token, "tok-123"));
}

// a rejected body leaves *s untrustworthy by contract, so each case starts fresh
static const char *apply_fresh(const char *body, bool setup) {
    settings_t s;
    settings_apply_t ap;
    memset(&s, 0, sizeof(s));
    return settings_json_apply(body, &s, setup, &ap);
}

static void test_rejects(void) {
    MT_ASSERT(apply_fresh("{\"name\":\"bad name\"}", false) != NULL);
    MT_ASSERT(apply_fresh("{\"ip_mode\":\"static\"}", false) != NULL);
    MT_ASSERT(apply_fresh("{\"ip\":\"10.0.0.256\"}", false) != NULL);
    MT_ASSERT(apply_fresh("{\"netmask\":\"255.0.255.0\",\"ip_mode\":\"static\","
                          "\"ip\":\"10.0.0.2\",\"gateway\":\"10.0.0.1\"}", false) != NULL);
    MT_ASSERT(apply_fresh("{\"port_boot\":[\"on\",\"maybe\"]}", false) != NULL);
    MT_ASSERT(apply_fresh("{\"port_priorities\":[0,300]}", false) != NULL);
    MT_ASSERT(apply_fresh("{\"syslog_port\":0}", false) != NULL);
    MT_ASSERT(apply_fresh("{\"port_sleep_min\":[0,1441]}", false) != NULL);
    MT_ASSERT(apply_fresh("{\"charged_min\":0}", false) != NULL);
    MT_ASSERT(apply_fresh("{\"port_auto_off\":[2]}", false) != NULL);
    MT_ASSERT(apply_fresh("{\"led_night\":\"22:00\"}", false) != NULL);
    MT_ASSERT(apply_fresh("{\"tz_offset_min\":900}", false) != NULL);
    MT_ASSERT(apply_fresh("{\"led_night\":\"\",\"led_idle_min\":60}", false) == NULL);
    MT_ASSERT(apply_fresh("{\"fan_mode\":\"on\",\"fan_on_w\":50,\"fan_off_w\":60}", false) != NULL);
    MT_ASSERT(apply_fresh("{\"name\":\"ok\"}", true) != NULL); // setup needs a token
    MT_ASSERT(apply_fresh("{\"name\":\"ok\",\"token\":\"t\"}", true) == NULL);
    // unknown keys are ignored; a partial body leaves the rest alone
    settings_t s;
    settings_apply_t ap;
    memset(&s, 0, sizeof(s));
    strcpy(s.mqtt_host, "keep");
    s.ip_dns = 7;
    MT_ASSERT(settings_json_apply("{\"fw\":\"9.9.9\",\"format\":1,\"dns\":\"\"}", &s, false, &ap) == NULL);
    MT_ASSERT(!strcmp(s.mqtt_host, "keep"));
    MT_ASSERT_EQ(s.ip_dns, 0); // an empty quad clears
}

static void test_ip4_text(void) {
    uint32_t a;
    char t[16];
    MT_ASSERT(ip4_parse("192.168.4.1", &a));
    ip4_format(t, sizeof(t), a);
    MT_ASSERT(!strcmp(t, "192.168.4.1"));
    MT_ASSERT(!ip4_parse("192.168.4", &a));
    MT_ASSERT(!ip4_parse("192.168.4.1.", &a));
    MT_ASSERT(!ip4_parse("1.2.3.0001", &a));
    MT_ASSERT(!ip4_parse("", &a));
    MT_ASSERT_EQ(ip4_format(t, sizeof(t), 0), 0);
    MT_ASSERT(ip4_mask_valid(ip("255.255.255.0")));
    MT_ASSERT(ip4_mask_valid(ip("255.255.255.255")));
    MT_ASSERT(ip4_mask_valid(ip("255.255.248.0")));
    MT_ASSERT(!ip4_mask_valid(ip("255.0.255.0")));
    MT_ASSERT(!ip4_mask_valid(ip("0.0.0.0")));
    MT_ASSERT(!ip4_mask_valid(ip("255.255.255.1")));
}

static void test_truncation_is_clean(void) {
    settings_t src;
    fill(&src);
    settings_json_opts_t o = {.secrets = true, .export = true};
    char small[200];
    MT_ASSERT_EQ(settings_json_build(small, sizeof(small), &src, &o), 0);
    MT_ASSERT_EQ(small[0], '\0');
}

void run_settings_json_tests(void) {
    mt_run("settings json: export -> import round trip", test_round_trip);
    mt_run("settings json: secrets only on request, kept on import", test_secrets_off_by_default);
    mt_run("settings json: bad bodies are refused whole", test_rejects);
    mt_run("settings json: dotted quads", test_ip4_text);
    mt_run("settings json: a buffer too small yields nothing", test_truncation_is_clean);
}
