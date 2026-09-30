#pragma once

// The board's power functions as ST's device layer calls them, for one
// source port behind a TCPP02. Names, types and return values are ST's
// (their X-NUCLEO-SRC1M1 board package); what is behind them is power.h.
//
// While the port is not armed (stack.h), whatever would make it visible or
// put power on it is refused or remembered, and applied when it is armed.

#include <stdint.h>

#include "stm32g0xx.h"

#define BSP_ERROR_NONE                    0
#define BSP_ERROR_NO_INIT                 (-1)
#define BSP_ERROR_WRONG_PARAM             (-2)
#define BSP_ERROR_BUSY                    (-3)
#define BSP_ERROR_PERIPH_FAILURE          (-4)
#define BSP_ERROR_COMPONENT_FAILURE       (-5)
#define BSP_ERROR_UNKNOWN_FAILURE         (-6)
#define BSP_ERROR_UNKNOWN_COMPONENT       (-7)
#define BSP_ERROR_BUS_FAILURE             (-8)
#define BSP_ERROR_CLOCK_FAILURE           (-9)
#define BSP_ERROR_MSP_FAILURE             (-10)
#define BSP_ERROR_FEATURE_NOT_SUPPORTED   (-11)

#define USBPD_PWR_INSTANCES_NBR           (1U)
#define USBPD_PWR_TYPE_C_PORT_1           (0U)
#define USBPD_PWR_TYPE_C_CC1              (1U)
#define USBPD_PWR_TYPE_C_CC2              (2U)

// vSafe5V and vSafe0V as the cable detection sees them
#define USBPD_PWR_HIGH_VBUS_THRESHOLD     (2800U)
#define USBPD_PWR_LOW_VBUS_THRESHOLD      (750U)

typedef enum {
    POWER_ROLE_SOURCE = 0,
    POWER_ROLE_SINK,
    POWER_ROLE_DUAL
} USBPD_PWR_PowerRoleTypeDef;

typedef enum {
    USBPD_PWR_MODE_OFF = 0,
    USBPD_PWR_MODE_HIBERNATE,
    USBPD_PWR_MODE_LOWPOWER,
    USBPD_PWR_MODE_NORMAL
} USBPD_PWR_PowerModeTypeDef;

typedef enum {
    VBUS_CONNECTED = 0,
    VBUS_NOT_CONNECTED
} USBPD_PWR_VBUSConnectionStatusTypeDef;

typedef void USBPD_PWR_VBUSDetectCallbackFunc(uint32_t Instance,
                                              USBPD_PWR_VBUSConnectionStatusTypeDef VBUSConnectionStatus);

int32_t BSP_USBPD_PWR_Init(uint32_t Instance);
int32_t BSP_USBPD_PWR_Deinit(uint32_t Instance);
int32_t BSP_USBPD_PWR_SetRole(uint32_t Instance, USBPD_PWR_PowerRoleTypeDef Role);
int32_t BSP_USBPD_PWR_SetPowerMode(uint32_t Instance, USBPD_PWR_PowerModeTypeDef PwrMode);
int32_t BSP_USBPD_PWR_GetPowerMode(uint32_t Instance, USBPD_PWR_PowerModeTypeDef *PwrMode);
int32_t BSP_USBPD_PWR_VBUSInit(uint32_t Instance);
int32_t BSP_USBPD_PWR_VBUSDeInit(uint32_t Instance);
int32_t BSP_USBPD_PWR_VBUSOn(uint32_t Instance);
int32_t BSP_USBPD_PWR_VBUSOff(uint32_t Instance);
int32_t BSP_USBPD_PWR_VBUSIsOn(uint32_t Instance, uint8_t *pState);
int32_t BSP_USBPD_PWR_VBUSGetVoltage(uint32_t Instance, uint32_t *pVoltage);
int32_t BSP_USBPD_PWR_VBUSGetCurrent(uint32_t Instance, int32_t *pCurrent);
int32_t BSP_USBPD_PWR_VCONNInit(uint32_t Instance, uint32_t CCPinId);
int32_t BSP_USBPD_PWR_VCONNDeInit(uint32_t Instance, uint32_t CCPinId);
int32_t BSP_USBPD_PWR_VCONNOn(uint32_t Instance, uint32_t CCPinId);
int32_t BSP_USBPD_PWR_VCONNOff(uint32_t Instance, uint32_t CCPinId);
int32_t BSP_USBPD_PWR_VCONNIsOn(uint32_t Instance, uint32_t CCPinId, uint8_t *pState);
int32_t BSP_USBPD_PWR_RegisterVBUSDetectCallback(uint32_t Instance,
                                                 USBPD_PWR_VBUSDetectCallbackFunc *pfnVBUSDetectCallback);
