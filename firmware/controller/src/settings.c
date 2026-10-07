#include "settings.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "settings_hw.h"
#include "update_latest.h"

// The store: which of the two sectors holds the record to use, how a record
// written by another firmware version is read, and where a save goes. All
// flash access is behind settings_hw.h, so the host tests run the whole of
// this against a pair of sectors in RAM.

#define SETTINGS_SLOTS      2
#define SETTINGS_VERSION    19

// Each older layout ended where the next version's fields begin, with its
// crc 4-byte aligned right after the last field. Accepting them means
// wifi/mqtt credentials survive a firmware upgrade. The asserts pin the
// on-flash offsets: they must never move once a version has shipped.
#define ALIGN4(x) (((x) + 3u) & ~3u)
#define SETTINGS_V1_PAYLOAD ALIGN4(offsetof(settings_t, fan_auto))
#define SETTINGS_V2_PAYLOAD ALIGN4(offsetof(settings_t, fan_on_ma))
#define SETTINGS_V3_PAYLOAD ALIGN4(offsetof(settings_t, led_boot))
#define SETTINGS_V4_PAYLOAD ALIGN4(offsetof(settings_t, port_name))
#define SETTINGS_V5_PAYLOAD ALIGN4(offsetof(settings_t, port_boot))
#define SETTINGS_V6_PAYLOAD ALIGN4(offsetof(settings_t, ip_static))
#define SETTINGS_V7_PAYLOAD ALIGN4(offsetof(settings_t, syslog_host))
#define SETTINGS_V8_PAYLOAD ALIGN4(offsetof(settings_t, charged_mw))
#define SETTINGS_V9_PAYLOAD ALIGN4(offsetof(settings_t, led_dim))
#define SETTINGS_V10_PAYLOAD ALIGN4(offsetof(settings_t, port_max_mv))
#define SETTINGS_V11_PAYLOAD ALIGN4(offsetof(settings_t, vin_cal))
#define SETTINGS_V12_PAYLOAD ALIGN4(offsetof(settings_t, blade_auto_update))
#define SETTINGS_V13_PAYLOAD ALIGN4(offsetof(settings_t, payload_len))
#define SETTINGS_V14_PAYLOAD ALIGN4(offsetof(settings_t, update_url))
#define SETTINGS_V15_PAYLOAD ALIGN4(offsetof(settings_t, ntp_server))
#define SETTINGS_V16_PAYLOAD ALIGN4(offsetof(settings_t, mqtt_tls))
#define SETTINGS_V17_PAYLOAD ALIGN4(offsetof(settings_t, port_protect))
#define SETTINGS_V18_PAYLOAD ALIGN4(offsetof(settings_t, hostnames))
_Static_assert(SETTINGS_V1_PAYLOAD == 376, "settings v1 layout moved");
_Static_assert(SETTINGS_V2_PAYLOAD == 380, "settings v2 layout moved");
_Static_assert(SETTINGS_V3_PAYLOAD == 384, "settings v3 layout moved");
_Static_assert(SETTINGS_V4_PAYLOAD == 384, "settings v4 layout moved"); // led_boot fit in v3's padding
_Static_assert(SETTINGS_V5_PAYLOAD == 528, "settings v5 layout moved");
_Static_assert(SETTINGS_V6_PAYLOAD == 536, "settings v6 layout moved");
_Static_assert(SETTINGS_V7_PAYLOAD == 552, "settings v7 layout moved");
_Static_assert(SETTINGS_V8_PAYLOAD == 620, "settings v8 layout moved");
_Static_assert(SETTINGS_V9_PAYLOAD == 636, "settings v9 layout moved");
_Static_assert(SETTINGS_V10_PAYLOAD == 644, "settings v10 layout moved");
_Static_assert(SETTINGS_V11_PAYLOAD == 656, "settings v11 layout moved");
_Static_assert(SETTINGS_V12_PAYLOAD == 660, "settings v12 layout moved");
_Static_assert(SETTINGS_V13_PAYLOAD == 664, "settings v13 layout moved");
// From version 14 the record carries its own length: payload_len sits in
// v13's tail padding, so v14 is as long as v13, and every later version
// keeps the field at this offset.
_Static_assert(offsetof(settings_t, payload_len) == 662, "settings payload_len moved");
_Static_assert(SETTINGS_V14_PAYLOAD == 664, "settings v14 layout moved");
_Static_assert(SETTINGS_V15_PAYLOAD == 728, "settings v15 layout moved");
_Static_assert(SETTINGS_V16_PAYLOAD == 792, "settings v16 layout moved");
_Static_assert(SETTINGS_V17_PAYLOAD == 2844, "settings v17 layout moved");
_Static_assert(SETTINGS_V18_PAYLOAD == 2848, "settings v18 layout moved");
_Static_assert(offsetof(settings_t, crc) == 2976, "settings v19 layout moved");
_Static_assert(sizeof(settings_t) <= SETTINGS_SECTOR_SIZE, "settings record outgrew its sector");

// Fan auto-policy defaults, shared by fresh defaults and version upgrades
#define FAN_ON_W_DEFAULT   80
#define FAN_OFF_W_DEFAULT  60
#define FAN_ON_MA_DEFAULT  3000
#define SYSLOG_PORT_DEFAULT 514
#define CHARGED_MW_DEFAULT  500 // a full phone trickles well under half a watt
#define CHARGED_MIN_DEFAULT 10
#define LED_DIM_DEFAULT     4   // just visible in a dark room
#define VIN_CAL_DEFAULT_    1000 // unity gain trim (vin.h VIN_CAL_DEFAULT)
// A controller away for longer than this (an update and its trial boot take
// well under a minute) is presumed gone: the blades go dark and wait in
// their bootloaders rather than run unsupervised.
#define BLADE_WATCH_S_DEFAULT 120

settings_t g_settings;

static uint32_t home_base;       // storage offset of the active ping-pong pair
static bool     migrate_pending; // valid legacy copy found while the new home is empty
static int      trial_slot = -1; // the one slot a trial image writes (settings_save)

static uint32_t crc32_calc(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static uint32_t payload_len(void) {
    return offsetof(settings_t, crc);
}

static uint32_t sector_off(uint32_t base, int i) {
    return base + (uint32_t)i * SETTINGS_SECTOR_SIZE;
}

static const settings_t *slot_ptr(uint32_t base, int i) {
    return (const settings_t *)settings_hw_sector(sector_off(base, i));
}

// Payload length of a stored record; its crc follows immediately. Layouts
// up to 13 are known by their version alone. A version newer than this
// firmware (it was reverted to after a trial of a newer image, or an older
// build was loaded on purpose) states its length itself: the fields this
// firmware knows are all there, at the offsets it knows, and the crc at the
// stated place proves the rest. Without that, a newer record was refused
// and the reverted firmware came up on defaults, credentials included.
static uint32_t record_payload_len(const settings_t *s) {
    switch (s->version) {
    case SETTINGS_VERSION: return payload_len();
    case 18:               return SETTINGS_V18_PAYLOAD;
    case 17:               return SETTINGS_V17_PAYLOAD;
    case 16:               return SETTINGS_V16_PAYLOAD;
    case 15:               return SETTINGS_V15_PAYLOAD;
    case 14:               return SETTINGS_V14_PAYLOAD;
    case 13:               return SETTINGS_V13_PAYLOAD;
    case 12:               return SETTINGS_V12_PAYLOAD;
    case 11:               return SETTINGS_V11_PAYLOAD;
    case 10:               return SETTINGS_V10_PAYLOAD;
    case 9:                return SETTINGS_V9_PAYLOAD;
    case 8:                return SETTINGS_V8_PAYLOAD;
    case 7:                return SETTINGS_V7_PAYLOAD;
    case 6:                return SETTINGS_V6_PAYLOAD;
    case 5:                return SETTINGS_V5_PAYLOAD;
    case 4:                return SETTINGS_V4_PAYLOAD;
    case 3:                return SETTINGS_V3_PAYLOAD;
    case 2:                return SETTINGS_V2_PAYLOAD;
    case 1:                return SETTINGS_V1_PAYLOAD;
    default:
        // a later layout states its length: it can only be longer than
        // this one, and must leave room in the sector for its crc
        if (s->version < SETTINGS_VERSION ||
            s->payload_len < payload_len() ||
            s->payload_len > SETTINGS_SECTOR_SIZE - sizeof(s->crc))
            return 0;
        return s->payload_len;
    }
}

static bool slot_valid(const settings_t *s) {
    if (s->magic != SETTINGS_MAGIC) return false;
    uint32_t len = record_payload_len(s);
    if (!len) return false;
    uint32_t crc;
    memcpy(&crc, (const uint8_t *)s + len, sizeof(crc));
    return crc32_calc((const uint8_t *)s, len) == crc;
}

static const settings_t *best_in(uint32_t base) {
    const settings_t *best = NULL;
    for (int i = 0; i < SETTINGS_SLOTS; i++) {
        const settings_t *s = slot_ptr(base, i);
        if (slot_valid(s) && (!best || s->seq > best->seq)) best = s;
    }
    return best;
}

void settings_defaults(void) {
    // Keep the sequence counter: a defaults+save must outrank the record it
    // replaces, and a fresh boot has seq 0 here anyway.
    uint32_t seq = g_settings.seq;
    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.magic = SETTINGS_MAGIC;
    g_settings.version = SETTINGS_VERSION;
    g_settings.seq = seq;
    strcpy(g_settings.device_name, "pwrman");
    g_settings.mqtt_port = 1883;
    g_settings.budget_mw = 360 * 1000; // 15A @ 24V; tune to the chassis supply
    for (int i = 0; i < NUM_PORTS; i++) {
        g_settings.port_limit_ma[i] = 5000;
        g_settings.port_max_mv[i] = PORT_VOLT_MAX_MV;
        g_settings.port_priority[i] = i;
    }
    g_settings.led_brightness = 48;
    g_settings.fan_auto = 1;
    g_settings.fan_on_w = FAN_ON_W_DEFAULT;
    g_settings.fan_off_w = FAN_OFF_W_DEFAULT;
    g_settings.fan_on_ma = FAN_ON_MA_DEFAULT;
    g_settings.syslog_port = SYSLOG_PORT_DEFAULT;
    g_settings.charged_mw = CHARGED_MW_DEFAULT;
    g_settings.charged_min = CHARGED_MIN_DEFAULT;
    g_settings.led_dim = LED_DIM_DEFAULT;
    g_settings.vin_cal = VIN_CAL_DEFAULT_;
    g_settings.blade_auto_update = 1;
    g_settings.blade_boot_via_loader = 1;
    g_settings.blade_watch_s = BLADE_WATCH_S_DEFAULT;
    strcpy(g_settings.update_url, UPDATE_SOURCE_DEFAULT);
}

void settings_load(void) {
    home_base = settings_hw_home();
    migrate_pending = false;
    trial_slot = -1;
    const settings_t *best = best_in(home_base);
    if (!best && home_base != settings_hw_legacy()) {
        best = best_in(settings_hw_legacy());
        migrate_pending = best != NULL;
    }
    if (best) {
        // A record of a newer layout is longer than this struct: the copy
        // takes the part this firmware knows, and the next save writes this
        // layout — that newer firmware defaults its own fields again when it
        // comes back, the same as after any upgrade.
        memcpy(&g_settings, best, sizeof(g_settings));
        // upgrade in place: default the fields the old layout lacked
        if (g_settings.version < 2) {
            g_settings.fan_auto = 1;
            g_settings.fan_on_w = FAN_ON_W_DEFAULT;
            g_settings.fan_off_w = FAN_OFF_W_DEFAULT;
        }
        if (g_settings.version < 3) g_settings.fan_on_ma = FAN_ON_MA_DEFAULT;
        if (g_settings.version < 4) g_settings.led_boot = LED_BOOT_WHITE;
        // the copy above read erased flash (0xFF) past the old record's end
        if (g_settings.version < 5) memset(g_settings.port_name, 0, sizeof(g_settings.port_name));
        if (g_settings.version < 6) {
            memset(g_settings.port_boot, PORT_BOOT_ON, sizeof(g_settings.port_boot));
            g_settings.port_off_mask = 0;
        }
        if (g_settings.version < 7) {
            g_settings.ip_static = 0;
            g_settings.ip_addr = g_settings.ip_mask = g_settings.ip_gw = g_settings.ip_dns = 0;
        }
        if (g_settings.version < 8) {
            g_settings.syslog_host[0] = '\0';
            g_settings.syslog_port = SYSLOG_PORT_DEFAULT;
        }
        if (g_settings.version < 9) {
            g_settings.charged_mw = CHARGED_MW_DEFAULT;
            g_settings.charged_min = CHARGED_MIN_DEFAULT;
            g_settings.port_auto_off = 0;
            memset(g_settings.port_sleep_min, 0, sizeof(g_settings.port_sleep_min));
        }
        if (g_settings.version < 10) {
            g_settings.led_dim = LED_DIM_DEFAULT;
            g_settings.led_night_start = g_settings.led_night_end = 0;
            g_settings.led_idle_min = 0;
            g_settings.tz_offset_min = 0;
        }
        if (g_settings.version < 11) {
            for (int i = 0; i < NUM_PORTS; i++) g_settings.port_max_mv[i] = PORT_VOLT_MAX_MV;
        }
        if (g_settings.version < 12) g_settings.vin_cal = VIN_CAL_DEFAULT_;
        if (g_settings.version < 13) {
            g_settings.blade_auto_update = 1;
            g_settings.blade_boot_via_loader = 1;
            g_settings.blade_watch_s = BLADE_WATCH_S_DEFAULT;
        }
        // the copy above put the old record's crc where this field starts
        if (g_settings.version < 15) {
            memset(g_settings.update_url, 0, sizeof(g_settings.update_url));
            strcpy(g_settings.update_url, UPDATE_SOURCE_DEFAULT);
        }
        // likewise a version 15 record's crc, or erased flash, sits here
        if (g_settings.version < 16) memset(g_settings.ntp_server, 0, sizeof(g_settings.ntp_server));
        if (g_settings.version < 17) {
            g_settings.mqtt_tls = 0;
            g_settings.mqtt_ca_len = 0;
            memset(g_settings.mqtt_ca, 0, sizeof(g_settings.mqtt_ca));
        }
        if (g_settings.version < 18) g_settings.port_protect = 0;
        if (g_settings.version < 19) memset(g_settings.hostnames, 0, sizeof(g_settings.hostnames));
        g_settings.version = SETTINGS_VERSION;
    } else {
        settings_defaults();
    }
}

bool settings_save(void) {
    static uint8_t buf[SETTINGS_SECTOR_SIZE]; // static: keep 4KB off the stack

    if (!home_base) home_base = settings_hw_home();

    // write to the slot NOT holding the current best copy
    int target = 0;
    const settings_t *s0 = slot_ptr(home_base, 0), *s1 = slot_ptr(home_base, 1);
    if (slot_valid(s0) && (!slot_valid(s1) || s0->seq > s1->seq)) target = 1;

    // An uncommitted trial image keeps to one slot, so the record the
    // previous firmware wrote is still there if the trial is reverted: that
    // firmware may not read this layout at all (one older than 0.11 reads
    // nothing newer than its own), and even when it does, a save that fails
    // halfway must not cost the last record it wrote. Changes made during
    // the trial are what is lost then, not the configuration.
    if (settings_hw_trial()) {
        if (trial_slot < 0) trial_slot = target;
        target = trial_slot;
    }

    g_settings.seq++;
    g_settings.magic = SETTINGS_MAGIC;
    g_settings.version = SETTINGS_VERSION;
    g_settings.payload_len = (uint16_t)payload_len();
    g_settings.crc = crc32_calc((const uint8_t *)&g_settings, payload_len());

    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, &g_settings, sizeof(g_settings));

    return settings_hw_write(sector_off(home_base, target), buf);
}

bool settings_wipe(void) {
    if (!home_base) home_base = settings_hw_home();
    uint32_t legacy = settings_hw_legacy();

    // A save only replaces the older slot, so the record before it (Wi-Fi
    // password, API token, broker login) would survive a reset in the other
    // one. Erase both, and a legacy copy too, before defaults go back in.
    // During a trial this also takes the record a revert would have read:
    // the older firmware then starts on defaults, which is what was asked.
    // A legacy sector is erased only when it holds a record, as in
    // settings_migrate, since on a partitioned board that flash is not ours.
    bool ok = true;
    for (int i = 0; i < SETTINGS_SLOTS; i++) {
        if (!settings_hw_write(sector_off(home_base, i), NULL)) ok = false;
        if (legacy != home_base && slot_valid(slot_ptr(legacy, i)) &&
            !settings_hw_write(sector_off(legacy, i), NULL))
            ok = false;
    }
    migrate_pending = false;
    trial_slot = -1;
    settings_defaults();
    return settings_save() && ok;
}

#define SAVE_DEBOUNCE_MS 5000

static uint32_t save_at_ms; // 0 = clean

void settings_save_later(void) {
    uint32_t t = settings_hw_now_ms() + SAVE_DEBOUNCE_MS;
    save_at_ms = t ? t : 1;
}

bool settings_save_pending(void) {
    return save_at_ms != 0;
}

int settings_save_poll(uint32_t now_ms) {
    if (!save_at_ms || (int32_t)(now_ms - save_at_ms) < 0) return 0;
    save_at_ms = 0;
    return settings_save() ? 1 : -1;
}

bool settings_migration_pending(void) {
    return migrate_pending;
}

bool settings_migrate(void) {
    if (!migrate_pending) return true;
    migrate_pending = false; // one attempt per boot; a manual 'save' also lands
                             // in the new home, so failure here loses nothing
    if (!settings_save()) return false;
    // Retire the legacy copies so old firmware or a stale sector can't
    // resurrect superseded credentials.
    for (int i = 0; i < SETTINGS_SLOTS; i++) {
        if (best_in(settings_hw_legacy()) == NULL) break;
        settings_hw_write(sector_off(settings_hw_legacy(), i), NULL);
    }
    return true;
}
