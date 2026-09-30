#pragma once

#include <stdbool.h>
#include <stdint.h>

// ST TCPP02-M18 source-port protection on the private bus (DS13787 Rev 4):
// CC line switches with over-voltage protection, the VCONN switch, the gate
// driver of the VBUS switch (Q3/Q4) with a fixed over-current trip across
// R27, VBUS discharge, and the IANA current monitor.
//
// It has no VBUS over-voltage protection and its VBUS pins stop at 24 V:
// that is the comparator pair on TCPP_EN (ovp.h). TCPP_EN low is the part's
// OFF state, which opens the switch and resets its registers, so after an
// over-voltage trip it starts from hibernate again.
//
// A VBUS over-current trip latches: gate driver and VCONN off, discharge on,
// FLGn low, until the control register is written again.

#define TCPP02_REG_CTRL   0 // write
#define TCPP02_REG_ACK    1 // read: the control bits as the part took them
#define TCPP02_REG_FLAGS  2 // read

#define TCPP02_CTRL_V1      (1u << 0) // VCONN switch to CC1
#define TCPP02_CTRL_V2      (1u << 1) // VCONN switch to CC2
#define TCPP02_CTRL_GDP     (1u << 2) // gate driver on: VBUS switch closed
#define TCPP02_CTRL_ONE     (1u << 3) // always written 1
#define TCPP02_CTRL_PM1     (1u << 4)
#define TCPP02_CTRL_PM2     (1u << 5)
#define TCPP02_CTRL_VBUSD   (1u << 6) // VBUS discharge
#define TCPP02_CTRL_VCONND  (1u << 7) // VCONN discharge

#define TCPP02_FLAG_OCP_VCONN (1u << 0)
#define TCPP02_FLAG_OCP_VBUS  (1u << 1)
#define TCPP02_FLAG_OTP       (1u << 3)
#define TCPP02_FLAG_OVP_CC    (1u << 4)
#define TCPP02_FLAG_VBUS_OK   (1u << 5)
#define TCPP02_FLAG_IS_TCPP02 (1u << 7) // 0 on a TCPP03

typedef enum {
    TCPP02_HIBERNATE = 0, // power-up state: CC switches open, gate driver off
    TCPP02_LOW_POWER,     // CC high-ohmic: attach detection only, no PD traffic
    TCPP02_NORMAL,
} tcpp02_mode_t;

typedef enum {
    TCPP02_VCONN_OFF = 0,
    TCPP02_VCONN_CC1,
    TCPP02_VCONN_CC2,
} tcpp02_vconn_t;

typedef struct {
    tcpp02_mode_t  mode;
    tcpp02_vconn_t vconn;
    bool vbus_on;         // gate driver; only acts in normal mode
    bool vbus_discharge;
    bool vconn_discharge;
} tcpp02_ctrl_t;

uint8_t tcpp02_ctrl_byte(const tcpp02_ctrl_t *c); // hardware-free

bool tcpp02_probe(void); // answers and identifies as a TCPP02
// Writes the control register and checks the acknowledge register took it.
// Also the recovery word after an over-current trip.
bool tcpp02_set(const tcpp02_ctrl_t *c);
bool tcpp02_read_flags(uint8_t *flags);
