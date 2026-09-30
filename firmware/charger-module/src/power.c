#include "power.h"

#include "blade.h"
#include "blade_regs.h"
#include "board.h"
#include "dac.h"
#include "hw.h"
#include "meter.h"
#include "ovp.h"
#include "tps55288.h"

#define TCPP_EN_SETTLE_MS  2
#define POLL_MS            2
#define SETTLE_PCT         5    // vSrcNew: within 5 % of the target...
#define SETTLE_FLOOR_MV    100  // ...or this, where 5 % is less

static bool port_up;       // we are driving TCPP_EN high
static bool conv_ready;
static bool vbus_on;
static uint32_t setpoint_mv;
static tcpp02_ctrl_t tcpp;

static void set_threshold(uint32_t trip_mv) {
    dac_set(ovp_dac_code(trip_mv, meter_vdda_mv()));
}

static bool tripped(void) {
    return port_up && (hw_ovp_trip_latched() || !hw_tcpp_en_sense());
}

static power_result_t write_tcpp(void) {
    if (!port_up) return POWER_NOT_READY;
    if (tripped()) return POWER_TRIPPED;
    return tcpp02_set(&tcpp) ? POWER_OK : POWER_BUS;
}

static bool conv_mode(bool on, bool discharge, bool forced_pwm) {
    tps55288_mode_t m = {.output_on = on, .discharge = discharge, .forced_pwm = forced_pwm};
    return tps55288_set_mode(&m);
}

static bool within(uint32_t mv, uint32_t target_mv) {
    uint32_t tol = (target_mv * SETTLE_PCT) / 100;
    if (tol < SETTLE_FLOOR_MV) tol = SETTLE_FLOOR_MV;
    return mv + tol >= target_mv && mv <= target_mv + tol;
}

static power_result_t wait_vbus(uint32_t target_mv, uint32_t timeout_ms) {
    uint32_t t0 = hw_ms();
    for (;;) {
        if (tripped()) return POWER_TRIPPED;
        uint32_t mv = meter_vbus_mv();
        if (target_mv ? within(mv, target_mv) : mv < POWER_VSAFE0V_MV) return POWER_OK;
        if (hw_ms() - t0 >= timeout_ms) return POWER_TIMEOUT;
        hw_delay_ms(POLL_MS);
    }
}

void power_init(void) {
    port_up = false;
    conv_ready = false;
    vbus_on = false;
    setpoint_mv = 0;
    tcpp = (tcpp02_ctrl_t){.mode = TCPP02_HIBERNATE};
}

power_result_t power_port_up(void) {
    set_threshold(ovp_threshold_mv(POWER_VSAFE5V_MV));
    hw_tcpp_en(true);
    hw_delay_ms(TCPP_EN_SETTLE_MS);
    hw_ovp_trip_clear(); // whatever the line did on its way up
    port_up = true;
    vbus_on = false;
    tcpp = (tcpp02_ctrl_t){.mode = TCPP02_HIBERNATE};
    if (!hw_tcpp_en_sense()) return POWER_TRIPPED;
    if (!tcpp02_probe()) return POWER_BUS;
    return write_tcpp();
}

void power_port_down(void) {
    port_up = false;
    vbus_on = false;
    hw_tcpp_en(false);
    tcpp = (tcpp02_ctrl_t){.mode = TCPP02_HIBERNATE};
}

power_result_t power_converter_up(void) {
    conv_ready = tps55288_probe() && tps55288_init(TPS55288_SR_2V5_PER_MS);
    setpoint_mv = 0;
    return conv_ready ? POWER_OK : POWER_BUS;
}

void power_converter_lost(void) {
    conv_ready = false;
    setpoint_mv = 0;
}

bool power_converter_ready(void) {
    return conv_ready;
}

power_result_t power_cc_mode(tcpp02_mode_t mode) {
    tcpp.mode = mode;
    if (mode != TCPP02_NORMAL) { // the gate driver and VCONN only exist in normal mode
        tcpp.vbus_on = false;
        tcpp.vconn = TCPP02_VCONN_OFF;
    }
    return write_tcpp();
}

power_result_t power_vconn(tcpp02_vconn_t cc) {
    tcpp.vconn = cc;
    tcpp.vconn_discharge = false;
    return write_tcpp();
}

power_result_t power_vbus_on(void) {
    if (!conv_ready) return POWER_NOT_READY;
    set_threshold(ovp_threshold_mv(POWER_VSAFE5V_MV));
    if (!tps55288_set_mv(POWER_VSAFE5V_MV) ||
        !tps55288_set_limit_ma(CABLE_LIMIT_AT_ATTACH_MA) ||
        !conv_mode(false, false, false))
        return POWER_BUS;

    // the switch first: the converter then ramps VBUS up from 0 V behind it
    tcpp.vbus_on = true;
    tcpp.vbus_discharge = false;
    power_result_t r = write_tcpp();
    if (r != POWER_OK) {
        tcpp.vbus_on = false;
        return r;
    }
    vbus_on = true;
    if (!conv_mode(true, false, false)) {
        power_shutdown();
        return POWER_BUS;
    }
    setpoint_mv = POWER_VSAFE5V_MV;
    r = wait_vbus(POWER_VSAFE5V_MV, POWER_SETTLE_TIMEOUT_MS);
    if (r != POWER_OK) power_shutdown();
    return r;
}

void power_shutdown(void) {
    if (conv_ready) conv_mode(false, true, false);
    setpoint_mv = 0;
    vbus_on = false;
    tcpp.vbus_on = false;
    tcpp.vconn = TCPP02_VCONN_OFF;
    if (port_up && !tripped()) {
        tcpp.vbus_discharge = true;
        tcpp02_set(&tcpp);
    }
}

power_result_t power_vbus_off(void) {
    uint32_t was_mv = setpoint_mv;
    bool conv_ok = !conv_ready || conv_mode(false, true, false);
    setpoint_mv = 0;
    vbus_on = false;
    tcpp.vbus_on = false;
    tcpp.vbus_discharge = true;
    power_result_t r = write_tcpp();
    if (r != POWER_OK) return r;

    r = wait_vbus(0, POWER_DISCHARGE_TIMEOUT_MS);
    tcpp.vbus_discharge = false;
    power_result_t w = write_tcpp();
    if (r == POWER_OK) r = w;
    if (r == POWER_OK && !conv_ok) r = POWER_BUS;
    // the threshold comes down only once the voltage it covered has gone
    if (r == POWER_OK || !was_mv) set_threshold(ovp_threshold_mv(POWER_VSAFE5V_MV));
    return r;
}

bool power_vbus_is_on(void) {
    return vbus_on;
}

power_result_t power_set(uint32_t mv, uint32_t ma, bool pps) {
    if (!conv_ready || !vbus_on) return POWER_NOT_READY;
    if (tripped()) return POWER_TRIPPED;

    uint32_t limit_ma = ma;
    if (!pps) {
        limit_ma = (ma * POWER_FIXED_MARGIN_PCT + 99) / 100;
        if (limit_ma < POWER_LIMIT_FLOOR_MA) limit_ma = POWER_LIMIT_FLOOR_MA;
    }

    uint32_t from_mv = setpoint_mv;
    bool down = mv < from_mv;
    set_threshold(ovp_transition_mv(from_mv, mv));
    if (down && !conv_mode(true, false, true)) return POWER_BUS;
    if (!tps55288_set_limit_ma(limit_ma) || !tps55288_set_mv(mv)) return POWER_BUS;
    setpoint_mv = mv;

    power_result_t r = wait_vbus(mv, POWER_SETTLE_TIMEOUT_MS);
    if (down && !conv_mode(true, false, false) && r == POWER_OK) r = POWER_BUS;
    if (r == POWER_OK) set_threshold(ovp_threshold_mv(mv));
    return r;
}

uint32_t power_setpoint_mv(void) {
    return setpoint_mv;
}

uint16_t power_poll_faults(void) {
    uint16_t faults = 0;

    if (tripped()) faults |= BLADE_FAULT_OVP;

    // FLGn means a failure in normal mode only; in the other two it reports
    // VBUS arriving from outside
    if (port_up && tcpp.mode == TCPP02_NORMAL && !(faults & BLADE_FAULT_OVP) && hw_port_fault()) {
        uint8_t flags;
        if (!tcpp02_read_flags(&flags)) {
            faults |= BLADE_FAULT_BUS;
        } else {
            if (flags & TCPP02_FLAG_OCP_VBUS) faults |= BLADE_FAULT_OCP_VBUS;
            if (flags & TCPP02_FLAG_OCP_VCONN) faults |= BLADE_FAULT_OCP_VCONN;
            if (flags & TCPP02_FLAG_OVP_CC) faults |= BLADE_FAULT_OVP_CC;
            if (flags & TCPP02_FLAG_OTP) faults |= BLADE_FAULT_OTP_PORT;
        }
    }

    if (conv_ready && hw_converter_fault()) {
        uint8_t status;
        if (!tps55288_read_status(&status)) {
            faults |= BLADE_FAULT_BUS;
        } else {
            if (status & TPS55288_STATUS_SCP) faults |= BLADE_FAULT_CONV_SCP;
            if (status & TPS55288_STATUS_OVP) faults |= BLADE_FAULT_CONV_OVP;
        }
    }
    return faults;
}
