#include <stddef.h>
#include <string.h>

#include "microtest.h"
#include "settings.h"
#include "settings_hw.h"

// The settings store against a flash in RAM: the ping-pong pair, records
// written by older and by NEWER firmware (a reverted trial leaves one), a
// trial image keeping to one slot, migration from the legacy location and
// the save debounce.

// Four sectors: the home pair at 0, the legacy pair at 0x2000, as on a
// partitioned board; `unpartitioned` puts the home on the legacy pair.
#define HOME   0u
#define LEGACY 0x2000u
#define SLOT(base, i) ((base) + (i) * SETTINGS_SECTOR_SIZE)

static uint8_t flash[4 * SETTINGS_SECTOR_SIZE];
static bool unpartitioned, trial, write_fail;
static unsigned writes;
static uint32_t now;

uint32_t settings_hw_home(void) { return unpartitioned ? LEGACY : HOME; }
uint32_t settings_hw_legacy(void) { return LEGACY; }
const void *settings_hw_sector(uint32_t off) { return flash + off; }
bool settings_hw_write(uint32_t off, const uint8_t *data) {
    writes++;
    if (write_fail) return false;
    memset(flash + off, 0xFF, SETTINGS_SECTOR_SIZE);
    if (data) memcpy(flash + off, data, SETTINGS_SECTOR_SIZE);
    return true;
}
bool settings_hw_trial(void) { return trial; }
uint32_t settings_hw_now_ms(void) { return now; }

// The record layout's own crc, kept separately here so the test also pins
// the polynomial a shipped record was written with.
static uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

#define V14_LEN 664u // a version 14 record: 0.11.0 wrote these
#define V15_LEN 728u // offsetof(settings_t, crc), pinned in settings.c

static void start(void) {
    memset(flash, 0xFF, sizeof(flash));
    unpartitioned = trial = write_fail = false;
    writes = 0;
    now = 0;
    memset(&g_settings, 0, sizeof(g_settings));
}

static const settings_t *rec(uint32_t off) { return (const settings_t *)(flash + off); }

static bool erased(uint32_t off) {
    for (unsigned i = 0; i < SETTINGS_SECTOR_SIZE; i++)
        if (flash[off + i] != 0xFF) return false;
    return true;
}

static bool crc_ok(uint32_t off, uint32_t len) {
    uint32_t c;
    memcpy(&c, flash + off + len, 4);
    return crc32(flash + off, len) == c;
}

// A record as another firmware would have written it: the struct's bytes
// up to `len` (0xAB where a longer layout's own fields would be), its crc
// there, erased flash beyond. The version and the length field are set on
// the copy, not on `s`.
static void write_record(uint32_t off, const settings_t *s, uint32_t version, uint32_t len) {
    uint8_t *sec = flash + off;
    memset(sec, 0xFF, SETTINGS_SECTOR_SIZE);
    memcpy(sec, s, sizeof(*s));
    if (len > sizeof(*s)) memset(sec + sizeof(*s), 0xAB, len - sizeof(*s));
    memcpy(sec + offsetof(settings_t, version), &version, 4);
    uint16_t plen = (uint16_t)len;
    if (version >= 14) memcpy(sec + offsetof(settings_t, payload_len), &plen, 2);
    uint32_t c = crc32(sec, len);
    memcpy(sec + len, &c, 4); // past the sector for a len that claims that: still RAM here
}

// Defaults with a few recognisable values, ready for write_record
static void sample(settings_t *s, const char *ssid, uint32_t seq) {
    settings_defaults();
    *s = g_settings;
    strcpy(s->wifi_ssid, ssid);
    strcpy(s->api_token, "tok");
    s->vin_cal = 1010;
    s->seq = seq;
}

static void test_empty_flash_gives_defaults(void) {
    start();
    settings_load();
    MT_ASSERT(strcmp(g_settings.device_name, "pwrman") == 0);
    MT_ASSERT_EQ(g_settings.version, 15);
    MT_ASSERT_EQ(g_settings.seq, 0);
    MT_ASSERT_EQ(g_settings.budget_mw, 360000);
    MT_ASSERT_EQ(g_settings.blade_watch_s, 120);
    MT_ASSERT(!settings_migration_pending());
    MT_ASSERT_EQ(writes, 0);
}

static void test_save_alternates_slots(void) {
    start();
    settings_load();
    strcpy(g_settings.wifi_ssid, "home");
    MT_ASSERT(settings_save());
    MT_ASSERT_EQ(writes, 1);
    MT_ASSERT_EQ(rec(SLOT(HOME, 0))->magic, SETTINGS_MAGIC);
    MT_ASSERT_EQ(rec(SLOT(HOME, 0))->version, 15);
    MT_ASSERT_EQ(rec(SLOT(HOME, 0))->seq, 1);
    MT_ASSERT_EQ(rec(SLOT(HOME, 0))->payload_len, V15_LEN);
    MT_ASSERT(crc_ok(SLOT(HOME, 0), V15_LEN));
    MT_ASSERT(erased(SLOT(HOME, 1)));

    strcpy(g_settings.wifi_ssid, "home2");
    MT_ASSERT(settings_save());
    MT_ASSERT_EQ(rec(SLOT(HOME, 1))->seq, 2);
    MT_ASSERT_EQ(rec(SLOT(HOME, 0))->seq, 1); // the previous copy stays

    memset(&g_settings, 0, sizeof(g_settings));
    settings_load();
    MT_ASSERT_EQ(g_settings.seq, 2);
    MT_ASSERT(strcmp(g_settings.wifi_ssid, "home2") == 0);

    MT_ASSERT(settings_save()); // back to slot 0
    MT_ASSERT_EQ(rec(SLOT(HOME, 0))->seq, 3);
}

static void test_corrupt_slot_falls_back(void) {
    start();
    settings_t s;
    sample(&s, "five", 5);
    write_record(SLOT(HOME, 0), &s, 15, V15_LEN);
    sample(&s, "six", 6);
    write_record(SLOT(HOME, 1), &s, 15, V15_LEN);
    flash[SLOT(HOME, 1) + 100] ^= 0x01; // a bit lost in the newer copy
    settings_load();
    MT_ASSERT_EQ(g_settings.seq, 5);
    MT_ASSERT(strcmp(g_settings.wifi_ssid, "five") == 0);
    MT_ASSERT(settings_save()); // over the corrupt one
    MT_ASSERT_EQ(rec(SLOT(HOME, 1))->seq, 6);
    MT_ASSERT(crc_ok(SLOT(HOME, 1), V15_LEN));
}

static void test_older_layout_upgrades(void) {
    start();
    settings_t s;
    sample(&s, "old", 7);
    s.blade_auto_update = s.blade_boot_via_loader = s.blade_watch_s = 0; // v12 had none of these
    write_record(SLOT(HOME, 0), &s, 12, 660);
    settings_load();
    MT_ASSERT(strcmp(g_settings.wifi_ssid, "old") == 0);
    MT_ASSERT(strcmp(g_settings.api_token, "tok") == 0);
    MT_ASSERT_EQ(g_settings.vin_cal, 1010);
    MT_ASSERT_EQ(g_settings.blade_auto_update, 1); // v13's defaults
    MT_ASSERT_EQ(g_settings.blade_watch_s, 120);
    MT_ASSERT_EQ(g_settings.version, 15);
    MT_ASSERT_EQ(g_settings.seq, 7);
    MT_ASSERT(settings_save());
    MT_ASSERT_EQ(rec(SLOT(HOME, 1))->version, 15);
    MT_ASSERT_EQ(rec(SLOT(HOME, 1))->seq, 8);
    MT_ASSERT_EQ(rec(SLOT(HOME, 1))->payload_len, V15_LEN);
    MT_ASSERT(crc_ok(SLOT(HOME, 1), V15_LEN));
}

static void test_oldest_layout_upgrades(void) {
    start();
    settings_t s;
    sample(&s, "first", 1);
    write_record(SLOT(HOME, 1), &s, 1, 376);
    settings_load();
    MT_ASSERT(strcmp(g_settings.wifi_ssid, "first") == 0);
    MT_ASSERT_EQ(g_settings.fan_on_w, 80);
    MT_ASSERT_EQ(g_settings.port_name[0][0], 0); // not the erased flash past v1's end
    MT_ASSERT_EQ(g_settings.port_boot[3], PORT_BOOT_ON);
    MT_ASSERT_EQ(g_settings.syslog_port, 514);
    MT_ASSERT_EQ(g_settings.port_max_mv[5], PORT_VOLT_MAX_MV);
    MT_ASSERT_EQ(g_settings.vin_cal, 1000);
    MT_ASSERT(strcmp(g_settings.update_url, "http://fw.powermanifold.io") == 0);
    MT_ASSERT_EQ(g_settings.version, 15);
}

// 0.11.0 wrote version 14: as long as 13, its crc where update_url now starts
static void test_v14_record_upgrades(void) {
    start();
    settings_t s;
    sample(&s, "eleven", 4);
    write_record(SLOT(HOME, 0), &s, 14, V14_LEN);
    settings_load();
    MT_ASSERT(strcmp(g_settings.wifi_ssid, "eleven") == 0);
    MT_ASSERT_EQ(g_settings.blade_watch_s, 120);
    MT_ASSERT(strcmp(g_settings.update_url, "http://fw.powermanifold.io") == 0); // not the old crc's bytes
    MT_ASSERT_EQ(g_settings.version, 15);
    MT_ASSERT(settings_save());
    MT_ASSERT_EQ(rec(SLOT(HOME, 1))->version, 15);
    MT_ASSERT_EQ(rec(SLOT(HOME, 1))->payload_len, V15_LEN);
    MT_ASSERT(crc_ok(SLOT(HOME, 1), V15_LEN));
    MT_ASSERT_EQ(rec(SLOT(HOME, 0))->version, 14); // what 0.11.0 would read after a revert stays
}

// A firmware reverted to after a trial of a newer one finds the trial's
// record: a later layout, longer than this struct, its length stated
static void test_newer_layout_is_read(void) {
    start();
    settings_t s;
    sample(&s, "future", 9);
    write_record(SLOT(HOME, 0), &s, 99, V15_LEN + 40);
    settings_load();
    MT_ASSERT(strcmp(g_settings.wifi_ssid, "future") == 0);
    MT_ASSERT(strcmp(g_settings.api_token, "tok") == 0);
    MT_ASSERT_EQ(g_settings.vin_cal, 1010);
    MT_ASSERT_EQ(g_settings.blade_watch_s, 120);
    MT_ASSERT_EQ(g_settings.version, 15);
    MT_ASSERT_EQ(g_settings.seq, 9);
    // the next save writes this layout, outranking the newer record
    MT_ASSERT(settings_save());
    MT_ASSERT_EQ(rec(SLOT(HOME, 1))->version, 15);
    MT_ASSERT_EQ(rec(SLOT(HOME, 1))->seq, 10);
    MT_ASSERT_EQ(rec(SLOT(HOME, 1))->payload_len, V15_LEN);
    MT_ASSERT(crc_ok(SLOT(HOME, 1), V15_LEN));
    memset(&g_settings, 0, sizeof(g_settings));
    settings_load();
    MT_ASSERT_EQ(g_settings.seq, 10);
}

static bool on_defaults(void) { return g_settings.wifi_ssid[0] == 0 && g_settings.api_token[0] == 0; }

static void test_newer_layout_needs_a_sane_length(void) {
    start();
    settings_t s;
    sample(&s, "future", 9);

    write_record(SLOT(HOME, 0), &s, 99, V15_LEN + 40);
    flash[SLOT(HOME, 0) + V15_LEN + 20] ^= 0x10; // one of its own fields corrupt
    settings_load();
    MT_ASSERT(on_defaults());

    // internally consistent records with a length no later layout can have:
    // shorter than this one, or leaving no room in the sector for the crc
    write_record(SLOT(HOME, 0), &s, 99, 600);
    settings_load();
    MT_ASSERT(on_defaults());
    write_record(SLOT(HOME, 0), &s, 99, SETTINGS_SECTOR_SIZE);
    settings_load();
    MT_ASSERT(on_defaults());
    write_record(SLOT(HOME, 0), &s, 99, SETTINGS_SECTOR_SIZE - 4); // the longest possible
    settings_load();
    MT_ASSERT(!on_defaults());
    MT_ASSERT_EQ(g_settings.seq, 9);

    // a version this firmware has no table for and that predates the field
    write_record(SLOT(HOME, 0), &s, 0, V15_LEN);
    settings_load();
    MT_ASSERT(on_defaults());
}

// While on trial, saves keep to one slot: the record the previous firmware
// wrote survives a revert
static void test_trial_keeps_to_one_slot(void) {
    start();
    settings_t s;
    sample(&s, "before", 3);
    write_record(SLOT(HOME, 0), &s, 15, V15_LEN);

    trial = true;
    settings_load();
    strcpy(g_settings.wifi_ssid, "during1");
    MT_ASSERT(settings_save());
    MT_ASSERT_EQ(rec(SLOT(HOME, 1))->seq, 4);
    strcpy(g_settings.wifi_ssid, "during2");
    MT_ASSERT(settings_save());
    MT_ASSERT_EQ(rec(SLOT(HOME, 1))->seq, 5); // the same slot again
    MT_ASSERT(strcmp(rec(SLOT(HOME, 1))->wifi_ssid, "during2") == 0);
    MT_ASSERT_EQ(rec(SLOT(HOME, 0))->seq, 3); // the pre-trial record, untouched
    MT_ASSERT(strcmp(rec(SLOT(HOME, 0))->wifi_ssid, "before") == 0);

    trial = false; // committed
    MT_ASSERT(settings_save());
    MT_ASSERT_EQ(rec(SLOT(HOME, 0))->seq, 6); // alternation resumes
    MT_ASSERT_EQ(rec(SLOT(HOME, 1))->seq, 5);
}

static void test_migration_from_legacy(void) {
    start();
    settings_t s;
    sample(&s, "legacy", 20);
    write_record(SLOT(LEGACY, 1), &s, 13, 664);
    settings_load();
    MT_ASSERT(strcmp(g_settings.wifi_ssid, "legacy") == 0);
    MT_ASSERT(settings_migration_pending());
    MT_ASSERT(settings_migrate());
    MT_ASSERT(!settings_migration_pending());
    MT_ASSERT_EQ(rec(SLOT(HOME, 0))->seq, 21);
    MT_ASSERT(strcmp(rec(SLOT(HOME, 0))->wifi_ssid, "legacy") == 0);
    MT_ASSERT(erased(SLOT(LEGACY, 0)));
    MT_ASSERT(erased(SLOT(LEGACY, 1)));
    MT_ASSERT(settings_migrate()); // nothing pending: fine
    MT_ASSERT_EQ(writes, 3);       // one save, two erases
}

static void test_home_record_outranks_legacy(void) {
    start();
    settings_t s;
    sample(&s, "legacy", 50);
    write_record(SLOT(LEGACY, 0), &s, 15, V15_LEN);
    sample(&s, "home", 2);
    write_record(SLOT(HOME, 0), &s, 15, V15_LEN);
    settings_load();
    MT_ASSERT(strcmp(g_settings.wifi_ssid, "home") == 0); // whatever the seq says
    MT_ASSERT(!settings_migration_pending());
}

static void test_unpartitioned_board_uses_legacy(void) {
    start();
    unpartitioned = true;
    settings_load();
    MT_ASSERT(!settings_migration_pending());
    strcpy(g_settings.wifi_ssid, "raw");
    MT_ASSERT(settings_save());
    MT_ASSERT_EQ(rec(SLOT(LEGACY, 0))->seq, 1);
    MT_ASSERT(erased(SLOT(HOME, 0)));
    settings_load();
    MT_ASSERT(strcmp(g_settings.wifi_ssid, "raw") == 0);
    MT_ASSERT(!settings_migration_pending());
}

static void test_defaults_keep_seq(void) {
    start();
    settings_t s;
    sample(&s, "x", 30);
    write_record(SLOT(HOME, 0), &s, 15, V15_LEN);
    settings_load();
    settings_defaults(); // a factory reset...
    MT_ASSERT_EQ(g_settings.wifi_ssid[0], 0);
    MT_ASSERT(settings_save()); // ...outranks the record it replaces
    MT_ASSERT_EQ(rec(SLOT(HOME, 1))->seq, 31);
    settings_load();
    MT_ASSERT_EQ(g_settings.wifi_ssid[0], 0);
}

static void test_save_later_debounces(void) {
    start();
    settings_load();
    now = 1000;
    settings_save_later();
    MT_ASSERT(settings_save_pending());
    MT_ASSERT_EQ(settings_save_poll(3000), 0);
    MT_ASSERT_EQ(writes, 0);
    MT_ASSERT_EQ(settings_save_poll(6000), 1);
    MT_ASSERT_EQ(writes, 1);
    MT_ASSERT(!settings_save_pending());
    MT_ASSERT_EQ(settings_save_poll(7000), 0);
}

static void test_write_failure_reports(void) {
    start();
    settings_load();
    write_fail = true;
    MT_ASSERT(!settings_save());
    settings_save_later();
    MT_ASSERT_EQ(settings_save_poll(10000), -1);
    MT_ASSERT(!settings_save_pending()); // one attempt; the next change re-arms it
}

void run_settings_tests(void) {
    mt_run("settings: empty flash gives defaults", test_empty_flash_gives_defaults);
    mt_run("settings: saves alternate the pair", test_save_alternates_slots);
    mt_run("settings: a corrupt slot falls back to the other", test_corrupt_slot_falls_back);
    mt_run("settings: an older layout upgrades in place", test_older_layout_upgrades);
    mt_run("settings: the oldest layout upgrades in place", test_oldest_layout_upgrades);
    mt_run("settings: a version 14 record gains the update source", test_v14_record_upgrades);
    mt_run("settings: a newer layout is read by its stated length", test_newer_layout_is_read);
    mt_run("settings: a newer layout needs a sane length and crc", test_newer_layout_needs_a_sane_length);
    mt_run("settings: a trial image keeps to one slot", test_trial_keeps_to_one_slot);
    mt_run("settings: legacy records migrate into the data partition", test_migration_from_legacy);
    mt_run("settings: a home record outranks a legacy one", test_home_record_outranks_legacy);
    mt_run("settings: an unpartitioned board uses the legacy pair", test_unpartitioned_board_uses_legacy);
    mt_run("settings: defaults keep the sequence", test_defaults_keep_seq);
    mt_run("settings: save_later debounces", test_save_later_debounces);
    mt_run("settings: a failed write is reported", test_write_failure_reports);
}
