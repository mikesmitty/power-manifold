#pragma once

#include <stdbool.h>
#include <stdint.h>

// Mean Well LAD-xxxU UPS supply on the controller card's UPS header (core 0,
// polled from the main loop). Probes at boot and keeps probing every few
// seconds while nothing answers, so the same image runs with or without a
// UPS fitted; once one answers, the status block and the three headline
// readings refresh every second, the per-block voltages and the
// undervoltage set-point every ten. Hardware-free above lad_hw.h.

typedef struct {
    bool     present;      // a supply answered within the last few seconds
    uint16_t status_h;     // LAD_STH_*
    uint16_t status_l;     // LAD_ST_*
    uint16_t mains_dv;     // AC input, 0.1 V
    uint16_t load_ca;      // load current, 0.01 A
    uint16_t batt_cv;      // battery string, 0.01 V
    uint16_t cell_cv[4];   // per-block, 0.01 V; 0xFFFF = tap not wired / not read yet
    uint16_t uvp_cv;       // undervoltage protection point, 0.01 V
    uint32_t last_ok_ms;   // when the last valid reply landed
    uint32_t replies, timeouts, bad_frames;
} ups_state_t;

void ups_init(void);
void ups_poll(uint32_t now_ms);

bool ups_present(void);
bool ups_on_battery(void);           // present and the load is on the battery
bool ups_fault(void);                // present and a LAD_ST_FAULT_MASK bit is set
const ups_state_t *ups_state(void);

// Battery-side conditions worth naming, comma-separated ("battery missing,
// undervoltage"); empty when none. Returns the number written.
unsigned ups_fault_text(char *buf, unsigned cap);

// One line for `info` / the page: "absent", or e.g.
// "on mains 230.2 V, battery 47.9 V charging, load 6.57 A"
const char *ups_status_str(void);

// Queue a buzzer on/off write (volatile on the supply: lost when it restarts).
// False if a write is already waiting.
bool ups_set_buzzer(bool on);
