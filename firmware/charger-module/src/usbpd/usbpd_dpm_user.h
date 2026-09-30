#pragma once

// The device policy manager's side of ST's stack: the callbacks the policy
// engine makes, and the settings types its configuration header fills in.
// Names are ST's.

#include "usbpd_def.h"

typedef struct {
    uint32_t XID; // assigned by the USB-IF
    uint16_t VID;
    uint16_t PID;
} USBPD_IdSettingsTypeDef;

typedef struct {
    uint32_t PE_DataSwap       : 1;
    uint32_t PE_VconnSwap      : 1;
    uint32_t PE_DR_Swap_To_DFP : 1;
    uint32_t PE_DR_Swap_To_UFP : 1;
    uint32_t Reserved1         : 28;
    USBPD_SCEDB_TypeDef DPM_SRCExtendedCapa;
} USBPD_USER_SettingsTypeDef;

USBPD_StatusTypeDef USBPD_DPM_UserInit(void);
void                USBPD_DPM_UserExecute(void const *argument);
void                USBPD_DPM_UserCableDetection(uint8_t PortNum, USBPD_CAD_EVENT State);
void                USBPD_DPM_UserTimerCounter(uint8_t PortNum);
void                USBPD_DPM_WaitForTime(uint32_t Time);

void                USBPD_DPM_Notification(uint8_t PortNum, USBPD_NotifyEventValue_TypeDef EventVal);
void                USBPD_DPM_HardReset(uint8_t PortNum, USBPD_PortPowerRole_TypeDef CurrentRole,
                                        USBPD_HR_Status_TypeDef Status);
USBPD_StatusTypeDef USBPD_DPM_SetupNewPower(uint8_t PortNum);
void                USBPD_DPM_ExtendedMessageReceived(uint8_t PortNum, USBPD_ExtendedMsg_TypeDef MsgType,
                                                      uint8_t *ptrData, uint16_t DataSize);
void                USBPD_DPM_GetDataInfo(uint8_t PortNum, USBPD_CORE_DataInfoType_TypeDef DataId,
                                          uint8_t *Ptr, uint32_t *Size);
void                USBPD_DPM_SetDataInfo(uint8_t PortNum, USBPD_CORE_DataInfoType_TypeDef DataId,
                                          uint8_t *Ptr, uint32_t Size);
USBPD_StatusTypeDef USBPD_DPM_EvaluateRequest(uint8_t PortNum, USBPD_CORE_PDO_Type_TypeDef *PtrPowerObject);
USBPD_StatusTypeDef USBPD_DPM_EvaluateVconnSwap(uint8_t PortNum);
USBPD_StatusTypeDef USBPD_DPM_PE_VconnPwr(uint8_t PortNum, USBPD_FunctionalState State);
void                USBPD_DPM_EnterErrorRecovery(uint8_t PortNum);
USBPD_StatusTypeDef USBPD_DPM_EvaluateDataRoleSwap(uint8_t PortNum);
USBPD_FunctionalState USBPD_DPM_IsPowerReady(uint8_t PortNum, USBPD_VSAFE_StatusTypeDef Vsafe);

USBPD_StatusTypeDef USBPD_VDM_UserInit(uint8_t PortNum);
