#include <stddef.h>

#include "usbpd_core.h"
#include "usbpd_dpm_conf.h"
#include "usbpd_dpm_user.h"
#include "usbpd_pwr_if.h"

#include "blade.h"
#include "port.h"

// Structured VDMs. The port takes part in none of its own; what it is here
// for is the policy engine's Discover Identity to the cable (SOP'), whose
// answer says what the cable carries.

static void inform_identity(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType,
                            USBPD_VDM_CommandType_Typedef CommandStatus,
                            USBPD_DiscoveryIdentity_TypeDef *pIdentity) {
    if (SOPType != USBPD_SOPTYPE_SOP1) return;
    switch (CommandStatus) {
    case SVDM_RESPONDER_ACK:
        if (pIdentity->CableVDO_Presence &&
            pIdentity->CableVDO.b.VBUS_CurrentHandCap == VBUS_5A && port_set_cable_ma(5000))
            blade_capabilities_stale = true;
        break;
    case SVDM_CABLE_NO_PD_CAPABLE:
        // Ra with nothing behind it: no e-marker to keep powered
        (void)USBPD_DPM_PE_VconnPwr(PortNum, USBPD_DISABLE);
        break;
    default:
        break;
    }
}

static USBPD_StatusTypeDef discover_identity(uint8_t PortNum, USBPD_DiscoveryIdentity_TypeDef *pIdentity) {
    (void)PortNum;
    (void)pIdentity;
    return USBPD_NAK;
}

static USBPD_StatusTypeDef discover_svids(uint8_t PortNum, uint16_t **p_SVID_Info, uint8_t *pNbSVID) {
    (void)PortNum;
    (void)p_SVID_Info;
    *pNbSVID = 0;
    return USBPD_NAK;
}

static USBPD_StatusTypeDef discover_modes(uint8_t PortNum, uint16_t SVID, uint32_t **p_ModeTab,
                                          uint8_t *NumberOfMode) {
    (void)PortNum;
    (void)SVID;
    (void)p_ModeTab;
    *NumberOfMode = 0;
    return USBPD_NAK;
}

static USBPD_StatusTypeDef mode_enter_exit(uint8_t PortNum, uint16_t SVID, uint32_t ModeIndex) {
    (void)PortNum;
    (void)SVID;
    (void)ModeIndex;
    return USBPD_NAK;
}

// Answers to requests the port never makes, and messages it has no use
// for: the stack calls these without asking whether they exist
static void inform_svid(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType,
                        USBPD_VDM_CommandType_Typedef CommandStatus, USBPD_SVIDInfo_TypeDef *pListSVID) {
    (void)PortNum;
    (void)SOPType;
    (void)CommandStatus;
    (void)pListSVID;
}

static void inform_mode(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType,
                        USBPD_VDM_CommandType_Typedef CommandStatus, USBPD_ModeInfo_TypeDef *pModesInfo) {
    (void)PortNum;
    (void)SOPType;
    (void)CommandStatus;
    (void)pModesInfo;
}

static void inform_mode_change(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType,
                               USBPD_VDM_CommandType_Typedef CommandStatus, uint16_t SVID,
                               uint32_t ModeIndex) {
    (void)PortNum;
    (void)SOPType;
    (void)CommandStatus;
    (void)SVID;
    (void)ModeIndex;
}

static void send_attention(uint8_t PortNum, uint8_t *pNbData, uint32_t *pVDO) {
    (void)PortNum;
    (void)pVDO;
    *pNbData = 0;
}

static void receive_attention(uint8_t PortNum, uint8_t NbData, uint32_t VDO) {
    (void)PortNum;
    (void)NbData;
    (void)VDO;
}

static void send_specific(uint8_t PortNum, USBPD_SOPType_TypeDef SOPType,
                          USBPD_VDM_Command_Typedef VDMCommand, uint8_t *pNbData, uint32_t *pVDO) {
    (void)PortNum;
    (void)SOPType;
    (void)VDMCommand;
    (void)pVDO;
    *pNbData = 0;
}

static USBPD_StatusTypeDef receive_specific(uint8_t PortNum, USBPD_VDM_Command_Typedef VDMCommand,
                                            uint8_t *pNbData, uint32_t *pVDO) {
    (void)PortNum;
    (void)VDMCommand;
    (void)pVDO;
    *pNbData = 0;
    return USBPD_NAK;
}

static void send_uvdm(uint8_t PortNum, USBPD_UVDMHeader_TypeDef *pUVDM_Header, uint8_t *pNbData,
                      uint32_t *pVDO) {
    (void)PortNum;
    (void)pUVDM_Header;
    (void)pVDO;
    *pNbData = 0;
}

static USBPD_StatusTypeDef receive_uvdm(uint8_t PortNum, USBPD_UVDMHeader_TypeDef UVDM_Header,
                                        uint8_t *pNbData, uint32_t *pVDO) {
    (void)PortNum;
    (void)UVDM_Header;
    (void)pVDO;
    *pNbData = 0;
    return USBPD_ERROR;
}

static USBPD_VDM_Callbacks callbacks = {
    .USBPD_VDM_DiscoverIdentity = discover_identity,
    .USBPD_VDM_DiscoverSVIDs = discover_svids,
    .USBPD_VDM_DiscoverModes = discover_modes,
    .USBPD_VDM_ModeEnter = mode_enter_exit,
    .USBPD_VDM_ModeExit = mode_enter_exit,
    .USBPD_VDM_InformIdentity = inform_identity,
    .USBPD_VDM_InformSVID = inform_svid,
    .USBPD_VDM_InformMode = inform_mode,
    .USBPD_VDM_InformModeEnter = inform_mode_change,
    .USBPD_VDM_InformModeExit = inform_mode_change,
    .USBPD_VDM_SendAttention = send_attention,
    .USBPD_VDM_ReceiveAttention = receive_attention,
    .USBPD_VDM_SendSpecific = send_specific,
    .USBPD_VDM_ReceiveSpecific = receive_specific,
    .USBPD_VDM_InformSpecific = send_specific,
    .USBPD_VDM_SendUVDM = send_uvdm,
    .USBPD_VDM_ReceiveUVDM = receive_uvdm,
};

USBPD_StatusTypeDef USBPD_VDM_UserInit(uint8_t PortNum) {
    USBPD_PE_InitVDM_Callback(PortNum, &callbacks);
    return USBPD_OK;
}
