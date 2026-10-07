#include "usbpd_dpm_user.h"

#include <string.h>

#include "usbpd_core.h"
#include "usbpd_dpm_conf.h"
#include "usbpd_dpm_core.h"
#include "usbpd_pwr_if.h"
#include "usbpd_pwr_user.h"

#include "hw.h"
#include "meter.h"
#include "port.h"
#include "power.h"
#include "sense.h"
#include "stack.h"
#include "supervisor.h"

// The policy engine's callbacks. They run in the main loop, between two
// passes of the supervisor; what they decide is decided in port.c and
// power.c.

static uint32_t received_rdo;
static bool reset_wanted;
static bool alert_wanted; // an over-temperature Alert the supervisor asked for

// Requests to the policy engine are refused while it is busy, so they are
// repeated until it takes them. New capabilities only need asking for inside
// a contract: until there is one the engine keeps sending them by itself,
// and reads the table each time.
static void ask_the_engine(void) {
    if (!port_attached()) {
        blade_capabilities_stale = false;
        reset_wanted = false;
        alert_wanted = false;
        return;
    }
    if (reset_wanted && USBPD_PE_Request_HardReset(USBPD_PORT_0) == USBPD_OK) reset_wanted = false;
    if (DPM_Params[USBPD_PORT_0].PE_Power != USBPD_POWER_EXPLICITCONTRACT) return;
    if (blade_capabilities_stale)
        (void)USBPD_PE_Request_DataMessage(USBPD_PORT_0, USBPD_DATAMSG_SRC_CAPABILITIES, NULL);
    if (alert_wanted) {
        // The Alert message exists from PD 3 on. The sink answers it with
        // Get_Status, which USBPD_DPM_GetDataInfo fills in below.
        if (DPM_Params[USBPD_PORT_0].PE_SpecRevision != USBPD_SPECIFICATION_REV3) {
            alert_wanted = false;
        } else {
            USBPD_ADO_TypeDef ado = {0};
            ado.b.TypeAlert = USBPD_ADO_TYPE_ALERT_OTP;
            if (USBPD_PE_Request_DataMessage(USBPD_PORT_0, USBPD_DATAMSG_ALERT, &ado.d32) == USBPD_OK)
                alert_wanted = false;
        }
    }
}

USBPD_StatusTypeDef USBPD_DPM_UserInit(void) {
    return USBPD_PWR_IF_Init();
}

void USBPD_DPM_UserExecute(void const *argument) {
    (void)argument;
    supervisor_run();
    ask_the_engine();
}

void USBPD_DPM_UserTimerCounter(uint8_t PortNum) {
    (void)PortNum;
}

void USBPD_DPM_WaitForTime(uint32_t Time) {
    hw_delay_ms(Time);
}

void USBPD_DPM_UserCableDetection(uint8_t PortNum, USBPD_CAD_EVENT State) {
    switch (State) {
    case USBPD_CAD_EVENT_ATTACHED:
    case USBPD_CAD_EVENT_ATTEMC:
        port_attach();
        if (USBPD_PWR_IF_VBUSEnable(PortNum) != USBPD_OK) supervisor_power_failed();
        break;
    default: // detached
        port_detach();
        (void)USBPD_PWR_IF_VBUSDisable(PortNum);
        if (DPM_Params[PortNum].VconnStatus == USBPD_TRUE)
            (void)USBPD_DPM_PE_VconnPwr(PortNum, USBPD_DISABLE);
        break;
    }
}

void USBPD_DPM_Notification(uint8_t PortNum, USBPD_NotifyEventValue_TypeDef EventVal) {
    (void)PortNum;
    switch (EventVal) {
    case USBPD_NOTIFY_HARDRESET_RX:
        port_hard_reset(true);
        break;
    case USBPD_NOTIFY_HARDRESET_TX:
        port_hard_reset(false);
        break;
    default:
        break;
    }
}

void USBPD_DPM_HardReset(uint8_t PortNum, USBPD_PortPowerRole_TypeDef CurrentRole,
                         USBPD_HR_Status_TypeDef Status) {
    (void)CurrentRole;
    switch (Status) {
    case USBPD_HR_STATUS_WAIT_VBUS_VSAFE0V:
        port_contract_lost();
        (void)USBPD_PWR_IF_VBUSDisable(PortNum);
        break;
    case USBPD_HR_STATUS_WAIT_VBUS_VSAFE5V:
        if (USBPD_PWR_IF_VBUSEnable(PortNum) != USBPD_OK) supervisor_power_failed();
        break;
    default:
        break;
    }
}

USBPD_StatusTypeDef USBPD_DPM_SetupNewPower(uint8_t PortNum) {
    return USBPD_PWR_IF_SetProfile(PortNum);
}

void USBPD_DPM_ExtendedMessageReceived(uint8_t PortNum, USBPD_ExtendedMsg_TypeDef MsgType,
                                       uint8_t *ptrData, uint16_t DataSize) {
    (void)PortNum;
    (void)MsgType;
    (void)ptrData;
    (void)DataSize;
}

void USBPD_DPM_GetDataInfo(uint8_t PortNum, USBPD_CORE_DataInfoType_TypeDef DataId,
                           uint8_t *Ptr, uint32_t *Size) {
    switch (DataId) {
    case USBPD_CORE_DATATYPE_SRC_PDO:
        USBPD_PWR_IF_GetPortPDOs(PortNum, DataId, Ptr, Size);
        break;
    case USBPD_CORE_DATATYPE_REQ_VOLTAGE: {
        uint32_t mv = power_setpoint_mv();
        *Size = 4;
        memcpy(Ptr, &mv, 4);
        break;
    }
    case USBPD_CORE_PPS_STATUS: {
        USBPD_PPSSDB_TypeDef st = {0};
        st.fields.OutputVoltageIn20mVunits = (uint16_t)(meter_vbus_mv() / 20);
        st.fields.OutputCurrentIn50mAunits = (uint8_t)(meter_iout_ma() / 50);
        st.fields.RealTimeFlags = USBPD_PPS_REALTIMEFLAGS_PTF_NOT_SUPPORTED |
                                  USBPD_PPS_REALTIMEFLAGS_OMF_DISABLED;
        *Size = 4;
        memcpy(Ptr, &st.d32, 4);
        break;
    }
    case USBPD_CORE_EXTENDED_CAPA:
        *Size = sizeof(USBPD_SCEDB_TypeDef);
        memcpy(Ptr, &DPM_USER_Settings[PortNum].DPM_SRCExtendedCapa, *Size);
        break;
    case USBPD_CORE_INFO_STATUS: {
        USBPD_SDB_TypeDef st = {0};
        int16_t dc = meter_temp_conv_dc();
        // degrees; 1 = below 2 degC, 0 = no reading
        if (dc != SENSE_TEMP_OPEN_DC) st.InternalTemp = dc < 20 ? 1 : (uint8_t)(dc / 10);
        st.PresentInput = USBPD_SDB_PRESENT_INPUT_EXT_PWR; // external power, DC
        switch (supervisor_temperature()) {
        case SUPERVISOR_TEMP_NORMAL:
            st.TemperatureStatus = USBPD_SDB_EVENT_TEMP_STATUS_NORMAL;
            break;
        case SUPERVISOR_TEMP_WARNING:
            st.TemperatureStatus = USBPD_SDB_EVENT_TEMP_STATUS_WARNING;
            st.EventFlags = USBPD_SDB_EVENT_FLAGS_OTP;
            break;
        default:
            st.TemperatureStatus = USBPD_SDB_EVENT_TEMP_STATUS_OVER_TEMP;
            st.EventFlags = USBPD_SDB_EVENT_FLAGS_OTP;
            break;
        }
        *Size = sizeof st;
        memcpy(Ptr, &st, *Size);
        break;
    }
    case USBPD_CORE_REVISION: {
        USBPD_RevisionDO_TypeDef rev = {
            .b.Revision_major = USBPD_REV_MAJOR,
            .b.Revision_minor = USBPD_REV_MINOR,
            .b.Version_major = USBPD_VERSION_MAJOR,
            .b.Version_minor = USBPD_VERSION_MINOR,
        };
        memcpy(Ptr, &rev, *Size);
        break;
    }
    default:
        *Size = 0;
        break;
    }
}

void USBPD_DPM_SetDataInfo(uint8_t PortNum, USBPD_CORE_DataInfoType_TypeDef DataId,
                           uint8_t *Ptr, uint32_t Size) {
    (void)PortNum;
    if (DataId == USBPD_CORE_DATATYPE_RCV_REQ_PDO && Size == 4) memcpy(&received_rdo, Ptr, 4);
}

USBPD_StatusTypeDef USBPD_DPM_EvaluateRequest(uint8_t PortNum, USBPD_CORE_PDO_Type_TypeDef *PtrPowerObject) {
    (void)PortNum;
    if (!stack_port_armed() || !port_request(received_rdo)) return USBPD_REJECT;
    *PtrPowerObject = port_pending()->pps ? USBPD_CORE_PDO_TYPE_APDO : USBPD_CORE_PDO_TYPE_FIXED;
    return USBPD_ACCEPT;
}

// the port is the VCONN source from attach on and stays it
USBPD_StatusTypeDef USBPD_DPM_EvaluateVconnSwap(uint8_t PortNum) {
    (void)PortNum;
    return USBPD_REJECT;
}

USBPD_StatusTypeDef USBPD_DPM_PE_VconnPwr(uint8_t PortNum, USBPD_FunctionalState State) {
    CCxPin_TypeDef cc = DPM_Params[PortNum].VconnCCIs;
    if (cc == CCNONE) return USBPD_ERROR;
    return State == USBPD_ENABLE ? USBPD_PWR_IF_Enable_VConn(PortNum, cc)
                                 : USBPD_PWR_IF_Disable_VConn(PortNum, cc);
}

void USBPD_DPM_EnterErrorRecovery(uint8_t PortNum) {
    port_contract_lost();
    (void)USBPD_PWR_IF_VBUSDisable(PortNum);
    USBPD_CAD_EnterErrorRecovery(PortNum);
}

// a charger has no data role to swap
USBPD_StatusTypeDef USBPD_DPM_EvaluateDataRoleSwap(uint8_t PortNum) {
    (void)PortNum;
    return USBPD_NOTSUPPORTED;
}

USBPD_FunctionalState USBPD_DPM_IsPowerReady(uint8_t PortNum, USBPD_VSAFE_StatusTypeDef Vsafe) {
    return USBPD_PWR_IF_SupplyReady(PortNum, Vsafe) == USBPD_OK ? USBPD_ENABLE : USBPD_DISABLE;
}

void stack_send_capabilities(void) {
    blade_capabilities_stale = true;
}

void stack_send_alert_otp(void) {
    alert_wanted = true;
}

void stack_hard_reset(void) {
    reset_wanted = true;
}

// ST's device layer and the policy engine tick in milliseconds off these
uint32_t HAL_GetTick(void) {
    return hw_ms();
}

void HAL_Delay(uint32_t Delay) {
    hw_delay_ms(Delay);
}
