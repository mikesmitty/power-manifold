#pragma once

// The power interface between ST's stack and the board: the capabilities
// offered, the profile a contract selects, and whether VBUS is where the
// stack expects it. Names are ST's.

#include "usbpd_def.h"

typedef enum {
    USBPD_PWR_BELOWVSAFE0V,
    USBPD_PWR_VSAFE5V,
    USBPD_PWR_SNKDETACH
} USBPD_VBUSPOWER_STATUS;

#define USBPD_PORT_IsValid(__Port__) ((__Port__) < (USBPD_PORT_COUNT))

USBPD_StatusTypeDef   USBPD_PWR_IF_Init(void);
USBPD_StatusTypeDef   USBPD_PWR_IF_SetProfile(uint8_t PortNum);
USBPD_StatusTypeDef   USBPD_PWR_IF_SupplyReady(uint8_t PortNum, USBPD_VSAFE_StatusTypeDef Vsafe);
USBPD_StatusTypeDef   USBPD_PWR_IF_VBUSEnable(uint8_t PortNum);
USBPD_StatusTypeDef   USBPD_PWR_IF_VBUSDisable(uint8_t PortNum);
USBPD_FunctionalState USBPD_PWR_IF_VBUSIsEnabled(uint8_t PortNum);
USBPD_StatusTypeDef   USBPD_PWR_IF_Enable_VConn(uint8_t PortNum, CCxPin_TypeDef CC);
USBPD_StatusTypeDef   USBPD_PWR_IF_Disable_VConn(uint8_t PortNum, CCxPin_TypeDef CC);
void                  USBPD_PWR_IF_GetPortPDOs(uint8_t PortNum, USBPD_CORE_DataInfoType_TypeDef DataId,
                                               uint8_t *Ptr, uint32_t *Size);
uint8_t               USBPD_PWR_IF_GetVBUSStatus(uint8_t PortNum, USBPD_VBUSPOWER_STATUS PowerTypeStatus);

// Not ST's. The table has changed since the policy engine last read it: set
// by whoever changes it, cleared when the engine fetches it for a
// Source_Capabilities message.
#include <stdbool.h>
extern bool blade_capabilities_stale;
