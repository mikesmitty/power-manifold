#include <stdint.h>

#include "usbpd_core.h"
#include "usbpd_dpm_conf.h"
#include "usbpd_dpm_core.h"
#include "usbpd_hw_if.h"

#include "adc.h"
#include "backplane.h"
#include "blade.h"
#include "console.h"
#include "dac.h"
#include "hw.h"
#include "port.h"
#include "power.h"
#include "private_bus.h"
#include "regmap.h"
#include "supervisor.h"

// The blade's firmware: ST's USB-PD stack as the port's policy engine, the
// supervisor as everything around it. The stack owns the main loop
// (USBPD_DPM_Run) and calls the supervisor once per turn.

void USBPD_PORT0_IRQHandler(void);

void UCPD1_2_IRQHandler(void) {
    USBPD_PORT0_IRQHandler();
}

static bool start_stack(void) {
    USBPD_HW_IF_GlobalHwInit();
    if (USBPD_DPM_InitCore() != USBPD_OK) return false;
    if (USBPD_DPM_UserInit() != USBPD_OK) return false;
    if (USBPD_VDM_UserInit(USBPD_PORT_0) != USBPD_OK) return false;
    if (USBPD_DPM_InitOS() != USBPD_OK) return false;
    hw_tick_hook = USBPD_DPM_TimerCounter;
    return true;
}

int main(void) {
    hw_init();
    regmap_init(hw_reset_cause());
    regmap_set_boot(hw_boot_via_loader() ? BLADE_BOOT_VIA_LOADER : 0);
    console_init();
    adc_init();
    dac_init();
    private_bus_init();
    backplane_init();

    console_str("\ncharger module " FW_VERSION ", reset cause ");
    console_hex(hw_reset_cause(), 2);
    console_str("\n");

    port_init();
    power_init();
    supervisor_init();

    if (start_stack()) USBPD_DPM_Run(); // does not return

    // Without the stack there is no port: the fault keeps it dark, and the
    // blade still answers the controller
    console_str("PD stack did not start\n");
    supervisor_stack_failed();
    for (;;) supervisor_run();
}
