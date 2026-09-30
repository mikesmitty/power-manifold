# CMSIS core and the STM32G0 device headers, startup file and system file
# (both Apache-2.0), fetched at the pinned tags. Nothing of ST's HAL is used.
# For a build without network access point FETCHCONTENT_SOURCE_DIR_CMSIS_CORE
# and FETCHCONTENT_SOURCE_DIR_CMSIS_DEVICE_G0 at local checkouts.
include(FetchContent)

FetchContent_Declare(cmsis_core
    GIT_REPOSITORY https://github.com/STMicroelectronics/cmsis_core
    GIT_TAG v5.9.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _none # headers only: nothing to add_subdirectory
)
FetchContent_Declare(cmsis_device_g0
    GIT_REPOSITORY https://github.com/STMicroelectronics/cmsis_device_g0
    GIT_TAG v1.4.5
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _none
)
FetchContent_MakeAvailable(cmsis_core cmsis_device_g0)

set(CMSIS_INCLUDE_DIRS
    ${cmsis_core_SOURCE_DIR}/Include
    ${cmsis_device_g0_SOURCE_DIR}/Include
)
set(CMSIS_SOURCES
    ${cmsis_device_g0_SOURCE_DIR}/Source/Templates/gcc/startup_stm32g071xx.s
    ${cmsis_device_g0_SOURCE_DIR}/Source/Templates/system_stm32g0xx.c
)
