#include "usbpd_pwr_user.h"

#include <stdbool.h>

#include "meter.h"
#include "power.h"
#include "stack.h"

static bool armed;
static USBPD_PWR_PowerModeTypeDef wanted = USBPD_PWR_MODE_HIBERNATE; // by the stack
static uint32_t vconn_pin; // USBPD_PWR_TYPE_C_CCx while VCONN is on, else 0

static int32_t result(power_result_t r) {
    switch (r) {
    case POWER_OK:        return BSP_ERROR_NONE;
    case POWER_BUS:       return BSP_ERROR_BUS_FAILURE;
    case POWER_NOT_READY: return BSP_ERROR_NO_INIT;
    default:              return BSP_ERROR_COMPONENT_FAILURE;
    }
}

static tcpp02_mode_t cc_mode(USBPD_PWR_PowerModeTypeDef mode) {
    switch (mode) {
    case USBPD_PWR_MODE_NORMAL:   return TCPP02_NORMAL;
    case USBPD_PWR_MODE_LOWPOWER: return TCPP02_LOW_POWER;
    default:                      return TCPP02_HIBERNATE;
    }
}

void stack_port_arm(void) {
    armed = true;
    (void)power_cc_mode(cc_mode(wanted));
}

void stack_port_disarm(void) {
    armed = false;
    vconn_pin = 0;
    power_shutdown();
    (void)power_cc_mode(TCPP02_HIBERNATE);
}

bool stack_port_armed(void) {
    return armed;
}

int32_t BSP_USBPD_PWR_Init(uint32_t Instance) {
    // TCPP_EN and the TCPP02 are brought up by the supervisor before the
    // stack starts, and again after every trip
    return Instance < USBPD_PWR_INSTANCES_NBR ? BSP_ERROR_NONE : BSP_ERROR_WRONG_PARAM;
}

int32_t BSP_USBPD_PWR_Deinit(uint32_t Instance) {
    (void)Instance;
    return BSP_ERROR_NONE;
}

int32_t BSP_USBPD_PWR_SetRole(uint32_t Instance, USBPD_PWR_PowerRoleTypeDef Role) {
    (void)Instance;
    if (Role != POWER_ROLE_SOURCE) return BSP_ERROR_WRONG_PARAM;
    if (wanted == USBPD_PWR_MODE_HIBERNATE)
        return BSP_USBPD_PWR_SetPowerMode(Instance, USBPD_PWR_MODE_LOWPOWER);
    return BSP_ERROR_NONE;
}

int32_t BSP_USBPD_PWR_SetPowerMode(uint32_t Instance, USBPD_PWR_PowerModeTypeDef PwrMode) {
    (void)Instance;
    wanted = PwrMode;
    if (!armed) return BSP_ERROR_NONE; // remembered for stack_port_arm
    return result(power_cc_mode(cc_mode(PwrMode)));
}

int32_t BSP_USBPD_PWR_GetPowerMode(uint32_t Instance, USBPD_PWR_PowerModeTypeDef *PwrMode) {
    (void)Instance;
    *PwrMode = armed ? wanted : USBPD_PWR_MODE_HIBERNATE;
    return BSP_ERROR_NONE;
}

// attach: the CC switches to full performance for the PD traffic
int32_t BSP_USBPD_PWR_VBUSInit(uint32_t Instance) {
    return BSP_USBPD_PWR_SetPowerMode(Instance, USBPD_PWR_MODE_NORMAL);
}

// detach
int32_t BSP_USBPD_PWR_VBUSDeInit(uint32_t Instance) {
    vconn_pin = 0;
    return BSP_USBPD_PWR_SetPowerMode(Instance, USBPD_PWR_MODE_LOWPOWER);
}

int32_t BSP_USBPD_PWR_VBUSOn(uint32_t Instance) {
    (void)Instance;
    if (!armed) return BSP_ERROR_NO_INIT;
    return result(power_vbus_on());
}

int32_t BSP_USBPD_PWR_VBUSOff(uint32_t Instance) {
    (void)Instance;
    if (!armed) return BSP_ERROR_NONE; // disarming switched it off already
    return result(power_vbus_off());
}

int32_t BSP_USBPD_PWR_VBUSIsOn(uint32_t Instance, uint8_t *pState) {
    (void)Instance;
    *pState = power_vbus_is_on() ? 1u : 0u;
    return BSP_ERROR_NONE;
}

int32_t BSP_USBPD_PWR_VBUSGetVoltage(uint32_t Instance, uint32_t *pVoltage) {
    (void)Instance;
    *pVoltage = meter_vbus_mv();
    return BSP_ERROR_NONE;
}

int32_t BSP_USBPD_PWR_VBUSGetCurrent(uint32_t Instance, int32_t *pCurrent) {
    (void)Instance;
    *pCurrent = (int32_t)meter_iout_ma();
    return BSP_ERROR_NONE;
}

int32_t BSP_USBPD_PWR_VCONNInit(uint32_t Instance, uint32_t CCPinId) {
    return BSP_USBPD_PWR_VCONNOff(Instance, CCPinId);
}

int32_t BSP_USBPD_PWR_VCONNDeInit(uint32_t Instance, uint32_t CCPinId) {
    return BSP_USBPD_PWR_VCONNOff(Instance, CCPinId);
}

int32_t BSP_USBPD_PWR_VCONNOn(uint32_t Instance, uint32_t CCPinId) {
    (void)Instance;
    if (!armed) return BSP_ERROR_NO_INIT;
    if (CCPinId != USBPD_PWR_TYPE_C_CC1 && CCPinId != USBPD_PWR_TYPE_C_CC2)
        return BSP_ERROR_WRONG_PARAM;
    power_result_t r =
        power_vconn(CCPinId == USBPD_PWR_TYPE_C_CC1 ? TCPP02_VCONN_CC1 : TCPP02_VCONN_CC2);
    if (r == POWER_OK) vconn_pin = CCPinId;
    return result(r);
}

int32_t BSP_USBPD_PWR_VCONNOff(uint32_t Instance, uint32_t CCPinId) {
    (void)Instance;
    (void)CCPinId;
    vconn_pin = 0;
    if (!armed) return BSP_ERROR_NONE;
    return result(power_vconn(TCPP02_VCONN_OFF));
}

int32_t BSP_USBPD_PWR_VCONNIsOn(uint32_t Instance, uint32_t CCPinId, uint8_t *pState) {
    (void)Instance;
    *pState = (vconn_pin && vconn_pin == CCPinId) ? 1u : 0u;
    return BSP_ERROR_NONE;
}

// The TCPP02 reports VBUS arriving from outside through this, for ports
// that can be sinks. A source-only port has nothing to report.
int32_t BSP_USBPD_PWR_RegisterVBUSDetectCallback(uint32_t Instance,
                                                 USBPD_PWR_VBUSDetectCallbackFunc *pfnVBUSDetectCallback) {
    (void)Instance;
    (void)pfnVBUSDetectCallback;
    return BSP_ERROR_NONE;
}
