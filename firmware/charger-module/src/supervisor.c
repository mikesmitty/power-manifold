#include "supervisor.h"

#include <stdbool.h>
#include <stdint.h>

#include "blade.h"
#include "blade_regs.h"
#include "console.h"
#include "hw.h"
#include "meter.h"
#include "port.h"
#include "power.h"
#include "regmap.h"
#include "sense.h"
#include "stack.h"

#define SAMPLE_MS          10
#define REPORT_MS          1000
// A temperature reading past its limit has to hold for this many samples
// before it counts. Noise cannot keep it there, a glitch from the switching
// stage might, and a latched fault costs a trip to the controller to clear.
#define TEMP_TRIP_SAMPLES  5
// EN high to the first word on the private bus. The TPS55288 datasheet gives
// no figure; this is the allowance the controller makes for the gen-2 blade.
#define CONVERTER_WAKE_MS  50

static regmap_live_t live;
static regmap_config_t cfg;
static bool en, conv_pending, line_up;
static bool leaving; // the MCU is on its way to a reset (only ever seen off the board)
static uint32_t en_ms, sample_ms, report_ms;
static uint32_t seen_ms, seen_count; // the controller's last transaction
static uint8_t hot_conv, hot_plug; // consecutive samples past the limit

static void raise(uint16_t faults) {
    hw_backplane_lock();
    regmap_raise(faults);
    hw_backplane_unlock();
}

static uint16_t faults_now(void) {
    hw_backplane_lock();
    uint16_t faults = regmap_faults();
    hw_backplane_unlock();
    return faults;
}

static uint16_t fault_of(power_result_t r) {
    switch (r) {
    case POWER_OK:      return 0;
    case POWER_TRIPPED: return BLADE_FAULT_OVP;
    case POWER_TIMEOUT: return BLADE_FAULT_VBUS;
    default:            return BLADE_FAULT_BUS;
    }
}

static void line_raise(void) {
    uint16_t f = fault_of(power_port_up());
    line_up = !f;
    if (f) {
        power_port_down();
        raise(f);
    }
}

static void follow_en(uint32_t now) {
    if (hw_en() != en) {
        en = !en;
        en_ms = now;
        conv_pending = en; // shutdown cost the converter its registers
        if (!en) power_converter_lost();
        console_str(en ? "EN high\n" : "EN low\n");
    }
    if (conv_pending && now - en_ms >= CONVERTER_WAKE_MS) {
        conv_pending = false;
        raise(fault_of(power_converter_up()));
    }
}

// The port goes dark before the MCU leaves: the ROM bootloader would not
// touch it, but the switch should not be found closed by whoever comes next.
static void leave_for_loader(bool program_boot_option) {
    console_str(program_boot_option ? "boot option: programming\n" : "reset: to the bootloader\n");
    leaving = true;
    stack_port_disarm();
    power_port_down();
    if (program_boot_option && !hw_boot_via_loader()) hw_program_boot_via_loader();
    else hw_reset_to_loader();
}

// A controller that has gone quiet for longer than it said it would (the
// WATCH_S register) gets the blade back in the bootloader, where it can be
// reached whatever the firmware was doing. Off until the controller sets it,
// so a blade on the bench keeps running.
static bool watch_controller(uint32_t now) {
    hw_backplane_lock();
    uint32_t n = regmap_transactions();
    hw_backplane_unlock();
    if (n != seen_count) {
        seen_count = n;
        seen_ms = now;
    }
    if (!cfg.watch_s || now - seen_ms < (uint32_t)cfg.watch_s * 1000u) return false;
    console_str("controller silent: ");
    leave_for_loader(false);
    return true;
}

static bool take_controller_input(void) {
    regmap_config_t c;
    hw_backplane_lock();
    bool changed = regmap_config(&c);
    uint8_t cmd = regmap_command();
    hw_backplane_unlock();

    if (changed) {
        cfg = c;
        console_str("config: port ");
        console_str(cfg.port_en ? "on, " : "off, ");
        console_dec(cfg.max_ma);
        console_str(" mA, ");
        console_dec(cfg.max_mv);
        console_str(" mV, watch ");
        console_dec(cfg.watch_s);
        console_str(" s\n");
        if (port_set_limits(cfg.max_ma, cfg.max_mv)) stack_send_capabilities();
    }
    switch (cmd) {
    case BLADE_CMD_SRC_CAP:
        stack_send_capabilities();
        break;
    case BLADE_CMD_HARD_RESET:
        stack_hard_reset();
        break;
    case BLADE_CMD_CLEAR_FAULTS: // the register file has cleared the latch
        if (!line_up) line_raise();
        break;
    case BLADE_CMD_RESET:
        leave_for_loader(false);
        return true;
    case BLADE_CMD_BOOT_OPT:
        leave_for_loader(true);
        return true;
    default:
        break;
    }
    return false;
}

static uint8_t hot_for(uint8_t samples, int16_t t_dc, int16_t limit_dc) {
    bool past = t_dc == SENSE_TEMP_OPEN_DC || t_dc > limit_dc;
    if (!past) return 0;
    return samples < TEMP_TRIP_SAMPLES ? (uint8_t)(samples + 1) : samples;
}

static uint16_t temperature_faults(void) {
    hot_conv = hot_for(hot_conv, live.temp_conv_dc, TEMP_LIMIT_CONV_DC);
    hot_plug = hot_for(hot_plug, live.temp_plug_dc, TEMP_LIMIT_PLUG_DC);
    uint16_t f = 0;
    if (hot_conv >= TEMP_TRIP_SAMPLES) f |= BLADE_FAULT_OT_CONV;
    if (hot_plug >= TEMP_TRIP_SAMPLES) f |= BLADE_FAULT_OT_PLUG;
    return f;
}

static void sample(void) {
    live.vbus_mv = (uint16_t)meter_vbus_mv();
    live.vout_mv = (uint16_t)meter_vout_mv();
    live.iout_ma = (uint16_t)meter_iout_ma();
    live.temp_conv_dc = meter_temp_conv_dc();
    live.temp_plug_dc = meter_temp_plug_dc();
    live.temp_mcu_dc = meter_temp_mcu_dc();
    live.status = (uint8_t)((en ? BLADE_ST_EN : 0) | (power_vbus_is_on() ? BLADE_ST_VBUS_ON : 0));
    port_report(&live);

    hw_backplane_lock();
    regmap_publish(&live);
    hw_backplane_unlock();
}

static void report(void) {
    console_str("en=");
    console_dec(en);
    console_str(" armed=");
    console_dec(stack_port_armed());
    console_str(" st=");
    console_hex(live.status, 2);
    console_str(" vdda=");
    console_dec((int32_t)meter_vdda_mv());
    console_str(" vout=");
    console_dec(live.vout_mv);
    console_str(" vbus=");
    console_dec(live.vbus_mv);
    console_str(" iout=");
    console_dec(live.iout_ma);
    console_str(" contract=");
    console_dec(live.contract_mv);
    console_str("/");
    console_dec(live.contract_ma);
    console_str(" conv=");
    console_tenths(live.temp_conv_dc);
    console_str(" plug=");
    console_tenths(live.temp_plug_dc);
    console_str(" fault=");
    console_hex(faults_now(), 4);
    console_str("\n");
}

void supervisor_init(void) {
    en = false;
    conv_pending = false;
    leaving = false;
    hot_conv = hot_plug = 0;
    sample_ms = report_ms = seen_ms = hw_ms();
    seen_count = regmap_transactions();
    sample();
    line_raise();
}

void supervisor_run(void) {
    if (leaving) return;
    hw_watchdog_feed();
    uint32_t now = hw_ms();

    follow_en(now);
    if (take_controller_input() || watch_controller(now)) return; // the MCU is resetting

    if (now - sample_ms >= SAMPLE_MS) {
        sample_ms = now;
        sample();
        raise(temperature_faults());
    }
    raise(power_poll_faults());

    bool alerting = (faults_now() & BLADE_FAULT_ALERTING) != 0;
    bool allowed = line_up && !alerting && en && power_converter_ready() && cfg.port_en &&
                   cfg.max_ma != 0;
    if (alerting && line_up) {
        stack_port_disarm();
        power_port_down();
        line_up = false;
    } else if (allowed != stack_port_armed()) {
        if (allowed) stack_port_arm();
        else stack_port_disarm();
    }

    hw_alert(alerting);
    // heartbeat: a short flash every second, a fast blink on a fault
    hw_led(alerting ? (now % 200) < 100 : (now % 1000) < 50);

    if (now - report_ms >= REPORT_MS) {
        report_ms = now;
        report();
    }
}

void supervisor_power_failed(void) {
    raise(BLADE_FAULT_VBUS);
}

void supervisor_stack_failed(void) {
    raise(BLADE_FAULT_PD);
}
