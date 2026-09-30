# ST's USB Power Delivery stack, the set X-CUBE-TCPP 4.2.0 ships together
# for the STM32G0 with a TCPP02:
#   - the core (policy engine, protocol layer): a binary library plus a
#     little source, licence SLA0044, for use on ST microcontrollers only
#   - the STM32G0 device layer (UCPD peripheral, cable detection), SLA0044
#   - ST's low-level drivers, which the device layer is written on (BSD-3)
# None of it is kept in this repository; all three are fetched at the pinned
# tags. For a build without network access point
# FETCHCONTENT_SOURCE_DIR_<NAME> at local checkouts.
include(FetchContent)

FetchContent_Declare(usbpd_core
    GIT_REPOSITORY https://github.com/STMicroelectronics/stm32-mw-usbpd-core
    GIT_TAG v5.3.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _none
)
FetchContent_Declare(usbpd_device_g0
    GIT_REPOSITORY https://github.com/STMicroelectronics/stm32-mw-usbpd-device-g0
    GIT_TAG v3.5.2
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _none
)
FetchContent_Declare(stm32g0_ll
    GIT_REPOSITORY https://github.com/STMicroelectronics/stm32g0xx_hal_driver
    GIT_TAG v1.4.5
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _none
)
FetchContent_MakeAvailable(usbpd_core usbpd_device_g0 stm32g0_ll)

# SPR: the smallest build of the core with both PPS and the structured VDMs
# that read a cable's e-marker. MINSRC has neither.
set(USBPD_CORE_CONFIG PD3_CONFIG_SPR)
set(USBPD_CORE_LIBRARY
    ${usbpd_core_SOURCE_DIR}/lib/USBPDCORE_${USBPD_CORE_CONFIG}_CM0PLUS_wc32.a)

set(USBPD_INCLUDE_DIRS
    ${usbpd_core_SOURCE_DIR}/inc
    ${usbpd_device_g0_SOURCE_DIR}/inc
    ${stm32g0_ll_SOURCE_DIR}/Inc
)
set(USBPD_SOURCES
    ${usbpd_core_SOURCE_DIR}/src/usbpd_dpm_core.c
    ${usbpd_core_SOURCE_DIR}/src/usbpd_trace.c
    ${usbpd_device_g0_SOURCE_DIR}/src/usbpd_cad_hw_if.c
    ${usbpd_device_g0_SOURCE_DIR}/src/usbpd_hw.c
    ${usbpd_device_g0_SOURCE_DIR}/src/usbpd_hw_if_it.c
    ${usbpd_device_g0_SOURCE_DIR}/src/usbpd_phy.c
    ${usbpd_device_g0_SOURCE_DIR}/src/usbpd_phy_hw_if.c
    ${usbpd_device_g0_SOURCE_DIR}/src/usbpd_pwr_hw_if.c
    ${usbpd_device_g0_SOURCE_DIR}/src/usbpd_timersserver.c
    ${stm32g0_ll_SOURCE_DIR}/Src/stm32g0xx_ll_dma.c
    ${stm32g0_ll_SOURCE_DIR}/Src/stm32g0xx_ll_ucpd.c
)
set(USBPD_DEFINITIONS
    USBPDCORE_LIB_${USBPD_CORE_CONFIG}
    USBPD_PORT_COUNT=1
    _SRC
    _VCONN_SUPPORT
    TCPP0203_SUPPORT
    USE_FULL_LL_DRIVER
)
