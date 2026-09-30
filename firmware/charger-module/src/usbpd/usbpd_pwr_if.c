#include "usbpd_pwr_if.h"

#include <string.h>

#include "usbpd_core.h"
#include "usbpd_dpm_conf.h"
#include "usbpd_dpm_core.h"
#include "usbpd_hw_if.h"
#include "usbpd_pwr_user.h"

#include "port.h"
#include "power.h"

bool blade_capabilities_stale;

USBPD_StatusTypeDef USBPD_PWR_IF_Init(void) {
    return USBPD_OK;
}

// The request the stack has accepted, delivered. Blocks until VBUS has
// arrived: the stack sends PS_RDY on the return.
USBPD_StatusTypeDef USBPD_PWR_IF_SetProfile(uint8_t PortNum) {
    (void)PortNum;
    const pdo_request_t *req = port_pending();
    if (!req) return USBPD_ERROR;
    if (power_set(req->mv, req->limit_ma, req->pps) != POWER_OK)
        return USBPD_ERROR;
    port_delivered();
    return USBPD_OK;
}

USBPD_StatusTypeDef USBPD_PWR_IF_SupplyReady(uint8_t PortNum, USBPD_VSAFE_StatusTypeDef Vsafe) {
    uint32_t mv;
    (void)BSP_USBPD_PWR_VBUSGetVoltage(PortNum, &mv);
    if (Vsafe == USBPD_VSAFE_0V) return mv < USBPD_PWR_LOW_VBUS_THRESHOLD ? USBPD_OK : USBPD_ERROR;
    return mv > USBPD_PWR_HIGH_VBUS_THRESHOLD ? USBPD_OK : USBPD_ERROR;
}

USBPD_StatusTypeDef USBPD_PWR_IF_VBUSEnable(uint8_t PortNum) {
    return HW_IF_PWR_Enable(PortNum, USBPD_ENABLE, CCNONE, USBPD_FALSE, USBPD_PORTPOWERROLE_SRC);
}

USBPD_StatusTypeDef USBPD_PWR_IF_VBUSDisable(uint8_t PortNum) {
    return HW_IF_PWR_Enable(PortNum, USBPD_DISABLE, CCNONE, USBPD_FALSE, USBPD_PORTPOWERROLE_SRC);
}

USBPD_FunctionalState USBPD_PWR_IF_VBUSIsEnabled(uint8_t PortNum) {
    return HW_IF_PWR_VBUSIsEnabled(PortNum);
}

static uint32_t cc_pin(CCxPin_TypeDef cc) {
    return cc == CC1 ? USBPD_PWR_TYPE_C_CC1 : USBPD_PWR_TYPE_C_CC2;
}

USBPD_StatusTypeDef USBPD_PWR_IF_Enable_VConn(uint8_t PortNum, CCxPin_TypeDef CC) {
    return BSP_USBPD_PWR_VCONNOn(PortNum, cc_pin(CC)) == BSP_ERROR_NONE ? USBPD_OK : USBPD_ERROR;
}

USBPD_StatusTypeDef USBPD_PWR_IF_Disable_VConn(uint8_t PortNum, CCxPin_TypeDef CC) {
    return BSP_USBPD_PWR_VCONNOff(PortNum, cc_pin(CC)) == BSP_ERROR_NONE ? USBPD_OK : USBPD_ERROR;
}

// Size comes back in bytes. To a PD 2.0 partner the PPS objects are left
// out: it has no notion of them.
void USBPD_PWR_IF_GetPortPDOs(uint8_t PortNum, USBPD_CORE_DataInfoType_TypeDef DataId,
                              uint8_t *Ptr, uint32_t *Size) {
    *Size = 0;
    if (DataId != USBPD_CORE_DATATYPE_SRC_PDO) return;
    blade_capabilities_stale = false;
    const pdo_table_t *t = port_table();
    bool rev2 = DPM_Params[PortNum].PE_SpecRevision == USBPD_SPECIFICATION_REV2;
    for (uint8_t i = 0; i < t->count; i++) {
        if (rev2 && pdo_is_pps(t->obj[i])) continue;
        memcpy(Ptr + *Size, &t->obj[i], 4);
        *Size += 4;
    }
}

uint8_t USBPD_PWR_IF_GetVBUSStatus(uint8_t PortNum, USBPD_VBUSPOWER_STATUS PowerTypeStatus) {
    uint32_t mv = HW_IF_PWR_GetVoltage(PortNum);
    switch (PowerTypeStatus) {
    case USBPD_PWR_BELOWVSAFE0V: return mv < USBPD_PWR_LOW_VBUS_THRESHOLD ? USBPD_TRUE : USBPD_FALSE;
    case USBPD_PWR_VSAFE5V:      return mv >= USBPD_PWR_HIGH_VBUS_THRESHOLD ? USBPD_TRUE : USBPD_FALSE;
    case USBPD_PWR_SNKDETACH:    return mv < USBPD_PWR_HIGH_VBUS_THRESHOLD ? USBPD_TRUE : USBPD_FALSE;
    default:                     return USBPD_FALSE;
    }
}
