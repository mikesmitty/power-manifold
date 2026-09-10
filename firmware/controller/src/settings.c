#include "settings.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/time.h"

#include "flash_map.h"

// The pair lives in the first two sectors of the "data" partition; boards
// without a partition table fall back to the legacy pre-partition location,
// the last two sectors of flash (which, on a freshly partitioned board, is
// where settings written by older firmware are found and migrated from).
#define SETTINGS_SLOTS      2
#define SETTINGS_VERSION    12

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

// Fan auto-policy defaults, shared by fresh defaults and version upgrades
#define FAN_ON_W_DEFAULT   80
#define FAN_OFF_W_DEFAULT  60
#define FAN_ON_MA_DEFAULT  3000
#define SYSLOG_PORT_DEFAULT 514
#define CHARGED_MW_DEFAULT  500 // a full phone trickles well under half a watt
#define CHARGED_MIN_DEFAULT 10
#define LED_DIM_DEFAULT     4   // just visible in a dark room
#define VIN_CAL_DEFAULT_    1000 // unity gain trim (vin.h VIN_CAL_DEFAULT)

#define LEGACY_BASE (PICO_FLASH_SIZE_BYTES - SETTINGS_SLOTS * FLASH_SECTOR_SIZE)

settings_t g_settings;

static uint32_t home_base;       // storage offset of the active ping-pong pair
static bool     migrate_pending; // valid legacy copy found while the new home is empty

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
    return base + (uint32_t)i * FLASH_SECTOR_SIZE;
}

// Reads go through the untranslated XIP alias: storage offsets stay valid no
// matter which A/B slot the bootrom mapped at XIP_BASE.
static const settings_t *slot_ptr(uint32_t base, int i) {
    return (const settings_t *)flash_map_xip_ptr(sector_off(base, i));
}

// Payload length of a stored layout version; its crc follows immediately.
static uint32_t version_payload_len(uint32_t version) {
    switch (version) {
    case SETTINGS_VERSION: return payload_len();
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
    default:               return 0;
    }
}

static bool slot_valid(const settings_t *s) {
    if (s->magic != SETTINGS_MAGIC) return false;
    uint32_t len = version_payload_len(s->version);
    if (!len) return false;
    uint32_t crc;
    memcpy(&crc, (const uint8_t *)s + len, sizeof(crc));
    return crc32_calc((const uint8_t *)s, len) == crc;
}

static uint32_t resolve_home(void) {
    uint32_t off, size;
    if (flash_map_find(FLASH_MAP_ID_DATA, &off, &size) &&
        size >= SETTINGS_SLOTS * FLASH_SECTOR_SIZE)
        return off;
    return LEGACY_BASE;
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
}

void settings_load(void) {
    home_base = resolve_home();
    const settings_t *best = best_in(home_base);
    if (!best && home_base != LEGACY_BASE) {
        best = best_in(LEGACY_BASE);
        migrate_pending = best != NULL;
    }
    if (best) {
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
        g_settings.version = SETTINGS_VERSION;
    } else {
        settings_defaults();
    }
}

typedef struct {
    uint32_t offset;
    const uint8_t *data; // NULL: erase only
} flash_op_t;

static void do_flash_write(void *param) {
    const flash_op_t *op = (const flash_op_t *)param;
    flash_range_erase(op->offset, FLASH_SECTOR_SIZE);
    if (op->data) flash_range_program(op->offset, op->data, FLASH_SECTOR_SIZE);
}

static bool run_flash_op(uint32_t offset, const uint8_t *data) {
    flash_op_t op = {.offset = offset, .data = data};
    // flash_safe_execute parks core 1 (the engine calls
    // flash_safe_execute_core_init at startup) while XIP is unavailable
    return flash_safe_execute(do_flash_write, &op, 500) == PICO_OK;
}

bool settings_save(void) {
    static uint8_t buf[FLASH_SECTOR_SIZE]; // static: keep 4KB off the stack

    if (!home_base) home_base = resolve_home();

    // write to the slot NOT holding the current best copy
    int target = 0;
    const settings_t *s0 = slot_ptr(home_base, 0), *s1 = slot_ptr(home_base, 1);
    if (slot_valid(s0) && (!slot_valid(s1) || s0->seq > s1->seq)) target = 1;

    g_settings.seq++;
    g_settings.crc = crc32_calc((const uint8_t *)&g_settings, payload_len());

    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, &g_settings, sizeof(g_settings));

    return run_flash_op(sector_off(home_base, target), buf);
}

#define SAVE_DEBOUNCE_MS 5000

static uint32_t save_at_ms; // 0 = clean

void settings_save_later(void) {
    uint32_t t = to_ms_since_boot(get_absolute_time()) + SAVE_DEBOUNCE_MS;
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
        if (best_in(LEGACY_BASE) == NULL) break;
        run_flash_op(sector_off(LEGACY_BASE, i), NULL);
    }
    return true;
}
