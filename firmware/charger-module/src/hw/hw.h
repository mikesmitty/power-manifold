#pragma once

#include <stdbool.h>
#include <stdint.h>

// The board's pins, tick and watchdog. Register-level, no vendor HAL.

// Clocks and pins. Every output lands in its safe state: TCPP_EN not
// driven high (the VBUS switch stays open), ALERT# released, LED off. The
// CC pins are left to the PD stack's device layer.
void hw_init(void);

// Called every millisecond from the tick interrupt, once set
extern void (*hw_tick_hook)(void);

uint32_t hw_ms(void);          // since hw_init, wraps after 49 days
void     hw_delay_ms(uint32_t ms); // feeds the watchdog while it waits
uint8_t  hw_reset_cause(void); // BLADE_RESET_*, sampled by hw_init
void     hw_watchdog_feed(void);

// Booting through the ROM bootloader (blade_regs.h BLADE_BOOT_VIA_LOADER).
bool hw_boot_via_loader(void);        // the option bytes as loaded send every reset there
// Reset into the ROM bootloader whatever the option bytes say: the flash is
// declared empty for the boot that follows, and the firmware clears that
// again when the controller starts it (RM0444 2.5.4). Does not return.
void hw_reset_to_loader(void);
// Program the option bytes so that every reset lands in the ROM bootloader,
// then reload them, which resets the MCU. Does not return.
void hw_program_boot_via_loader(void);

bool hw_en(void);              // the slot's EN line: the converter is out of shutdown
bool hw_converter_fault(void); // TPS55288 FB/INT low
bool hw_port_fault(void);      // TCPP02 FLGn low
bool hw_tcpp_en_sense(void);   // TCPP_EN as it stands: low while an OVP comparator holds it
// A falling edge of TCPP_EN is latched, however short: the comparator lets
// go again as soon as the output has fallen below the threshold
bool hw_ovp_trip_latched(void);
void hw_ovp_trip_clear(void);

void hw_tcpp_en(bool on);      // PA15 through R8; the comparators can still pull the line low
void hw_alert(bool asserted);  // ALERT#, open drain
void hw_led(bool on);

// Around the main loop's calls into the register file
void hw_backplane_lock(void);
void hw_backplane_unlock(void);
