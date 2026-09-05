#include "settings.h"

#include <stdio.h>
#include <string.h>

// The hardware-free half of settings: text helpers and bookkeeping that the
// host tests link without flash.

const char *settings_port_name(unsigned port) {
    static char fallback[NUM_PORTS][8];
    if (port >= NUM_PORTS) return "?";
    if (g_settings.port_name[port][0]) return g_settings.port_name[port];
    if (!fallback[port][0]) snprintf(fallback[port], sizeof(fallback[port]), "Port %u", port + 1);
    return fallback[port];
}

bool settings_port_name_valid(const char *s) {
    size_t n = strlen(s);
    if (n > PORT_NAME_MAX) return false;
    if (n && (s[0] == ' ' || s[n - 1] == ' ')) return false;
    for (; *s; s++) {
        if ((unsigned char)*s < 0x20 || (unsigned char)*s == 0x7F) return false;
    }
    return true;
}

bool settings_port_admin_note(unsigned port, bool on) {
    if (port >= NUM_PORTS) return false;
    uint8_t bit = (uint8_t)(1u << port);
    uint8_t was = g_settings.port_off_mask;
    if (on) g_settings.port_off_mask &= (uint8_t)~bit;
    else g_settings.port_off_mask |= bit;
    return g_settings.port_boot[port] == PORT_BOOT_LAST && g_settings.port_off_mask != was;
}

const char *settings_port_boot_name(uint8_t policy) {
    switch (policy) {
    case PORT_BOOT_OFF:  return "off";
    case PORT_BOOT_LAST: return "last";
    default:             return "on";
    }
}

bool settings_port_boot_parse(const char *s, uint8_t *policy) {
    if (!strcmp(s, "on")) *policy = PORT_BOOT_ON;
    else if (!strcmp(s, "off")) *policy = PORT_BOOT_OFF;
    else if (!strcmp(s, "last")) *policy = PORT_BOOT_LAST;
    else return false;
    return true;
}
