#include "settings.h"

#include <stddef.h>
#include <string.h>

#include "hardware/flash.h"
#include "pico/flash.h"

// Two dedicated sectors at the top of flash, clear of the program image and
// of any future A/B OTA partition layout (revisit when the partition table
// lands).
#define SETTINGS_SLOTS      2
#define SETTINGS_SECTOR(i)  (PICO_FLASH_SIZE_BYTES - ((i) + 1) * FLASH_SECTOR_SIZE)
#define SETTINGS_VERSION    1

settings_t g_settings;

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

static const settings_t *slot_ptr(int i) {
    return (const settings_t *)(XIP_BASE + SETTINGS_SECTOR(i));
}

static bool slot_valid(const settings_t *s) {
    if (s->magic != SETTINGS_MAGIC || s->version != SETTINGS_VERSION) return false;
    return crc32_calc((const uint8_t *)s, payload_len()) == s->crc;
}

void settings_defaults(void) {
    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.magic = SETTINGS_MAGIC;
    g_settings.version = SETTINGS_VERSION;
    g_settings.seq = 0;
    strcpy(g_settings.device_name, "pwrman");
    g_settings.mqtt_port = 1883;
    g_settings.budget_mw = 360 * 1000; // 15A @ 24V; tune to the chassis supply
    for (int i = 0; i < NUM_PORTS; i++) {
        g_settings.port_limit_ma[i] = 5000;
        g_settings.port_priority[i] = i;
    }
    g_settings.led_brightness = 48;
}

void settings_load(void) {
    const settings_t *best = NULL;
    for (int i = 0; i < SETTINGS_SLOTS; i++) {
        const settings_t *s = slot_ptr(i);
        if (slot_valid(s) && (!best || s->seq > best->seq)) best = s;
    }
    if (best) {
        memcpy(&g_settings, best, sizeof(g_settings));
    } else {
        settings_defaults();
    }
}

typedef struct {
    uint32_t offset;
    const uint8_t *data;
} flash_op_t;

static void do_flash_write(void *param) {
    const flash_op_t *op = (const flash_op_t *)param;
    flash_range_erase(op->offset, FLASH_SECTOR_SIZE);
    flash_range_program(op->offset, op->data, FLASH_SECTOR_SIZE);
}

bool settings_save(void) {
    static uint8_t buf[FLASH_SECTOR_SIZE]; // static: keep 4KB off the stack

    // write to the slot NOT holding the current best copy
    int target = 0;
    const settings_t *s0 = slot_ptr(0), *s1 = slot_ptr(1);
    if (slot_valid(s0) && (!slot_valid(s1) || s0->seq > s1->seq)) target = 1;

    g_settings.seq++;
    g_settings.crc = crc32_calc((const uint8_t *)&g_settings, payload_len());

    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, &g_settings, sizeof(g_settings));

    flash_op_t op = {
        .offset = SETTINGS_SECTOR(target),
        .data = buf,
    };
    // flash_safe_execute parks core 1 (the engine calls
    // flash_safe_execute_core_init at startup) while XIP is unavailable
    return flash_safe_execute(do_flash_write, &op, 500) == PICO_OK;
}
