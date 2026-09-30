#pragma once

// The stack's settings for the blade: one source-only port. ST's
// usbpd_dpm_core.c includes this with __USBPD_DPM_CORE_C defined and gets
// the definitions; everyone else gets the declarations.

#include "usbpd_dpm_user.h"

#include "blade.h"

// Without a USB-IF vendor ID of one's own the specification has 0xFFFF sent
#define USBPD_VID (0xFFFFu)
#define USBPD_PID (0x0000u)
#define USBPD_XID (0x00000000u)

#ifndef __USBPD_DPM_CORE_C
extern USBPD_SettingsTypeDef      DPM_Settings[USBPD_PORT_COUNT];
extern USBPD_IdSettingsTypeDef    DPM_ID_Settings[USBPD_PORT_COUNT];
extern USBPD_USER_SettingsTypeDef DPM_USER_Settings[USBPD_PORT_COUNT];
#else
USBPD_SettingsTypeDef DPM_Settings[USBPD_PORT_COUNT] = {
    {
        // SOP' as well: the cable's e-marker decides whether 5 A is offered
        .PE_SupportedSOP = USBPD_SUPPORTED_SOP_SOP | USBPD_SUPPORTED_SOP_SOP1,
        .PE_SpecRevision = USBPD_SPECIFICATION_REV3,
        .PE_DefaultRole = USBPD_PORTPOWERROLE_SRC,
        .PE_RoleSwap = USBPD_FALSE,
        .PE_VDMSupport = USBPD_TRUE,
        .PE_RespondsToDiscovSOP = USBPD_FALSE,
        .PE_AttemptsDiscovSOP = USBPD_FALSE,
        .PE_PingSupport = USBPD_FALSE,
        .PE_CapscounterSupport = USBPD_FALSE,
        .CAD_RoleToggle = USBPD_FALSE,
        .CAD_TryFeature = USBPD_FALSE,
        .CAD_AccesorySupport = USBPD_FALSE,
        .PE_PD3_Support.d = {
            .PE_UnchunkSupport = USBPD_FALSE,
            .PE_FastRoleSwapSupport = USBPD_FALSE,
            .Is_GetPPSStatus_Supported = USBPD_TRUE,
            .Is_SrcCapaExt_Supported = USBPD_TRUE,
            .Is_Alert_Supported = USBPD_FALSE,
            .Is_GetStatus_Supported = USBPD_TRUE,
            .Is_GetManufacturerInfo_Supported = USBPD_FALSE,
            .Is_GetCountryCodes_Supported = USBPD_FALSE,
            .Is_GetCountryInfo_Supported = USBPD_FALSE,
            .Is_SecurityRequest_Supported = USBPD_FALSE,
            .Is_FirmUpdateRequest_Supported = USBPD_FALSE,
            .Is_GetBattery_Supported = USBPD_FALSE,
        },
        .CAD_DefaultResistor = vRp_3_0A,
        .CAD_SRCToggleTime = 0,
        .CAD_SNKToggleTime = 0,
    },
};

USBPD_IdSettingsTypeDef DPM_ID_Settings[USBPD_PORT_COUNT] = {
    {.XID = USBPD_XID, .VID = USBPD_VID, .PID = USBPD_PID},
};

USBPD_USER_SettingsTypeDef DPM_USER_Settings[USBPD_PORT_COUNT] = {
    {
        .PE_DataSwap = USBPD_FALSE,
        .PE_VconnSwap = USBPD_FALSE,
        .PE_DR_Swap_To_DFP = USBPD_FALSE,
        .PE_DR_Swap_To_UFP = USBPD_FALSE,
        .DPM_SRCExtendedCapa = {
            .VID = USBPD_VID,
            .PID = USBPD_PID,
            .XID = USBPD_XID,
            .FW_revision = FW_MAJOR,
            .HW_revision = 3, // the gen-3 blade
            .Source_inputs = 0x03, // an external supply, unconstrained: as the 5 V object says
            .SourcePDP = PORT_POWER_MAX_MW / 1000,
        },
    },
};
#endif /* __USBPD_DPM_CORE_C */
