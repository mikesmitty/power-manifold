cmake_minimum_required(VERSION 3.18)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake)
endif()

project(power_manifold_charger_module C ASM)
set(CMAKE_C_STANDARD 11)

include(cmake/version.cmake)
include(cmake/cmsis.cmake)
include(cmake/st_usbpd.cmake)

option(BLADE_WATCHDOG "Independent watchdog (frozen while a debugger holds the core)" ON)

add_executable(charger-module
    src/main.c
    src/ovp.c
    src/pdo.c
    src/port.c
    src/power.c
    src/regmap.c
    src/sense.c
    src/supervisor.c
    src/tcpp02.c
    src/tps55288.c
    src/hw/adc.c
    src/hw/backplane.c
    src/hw/console.c
    src/hw/dac.c
    src/hw/hw.c
    src/hw/meter.c
    src/hw/private_bus.c
    src/image_header.c
    src/usbpd/usbpd_dpm_user.c
    src/usbpd/usbpd_pwr_if.c
    src/usbpd/usbpd_pwr_user.c
    src/usbpd/usbpd_vdm_user.c
    ${CMSIS_SOURCES}
    ${USBPD_SOURCES}
)
set_target_properties(charger-module PROPERTIES SUFFIX .elf)

target_include_directories(charger-module PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/hw
    ${CMAKE_CURRENT_LIST_DIR}/src/usbpd
)
target_include_directories(charger-module SYSTEM PRIVATE ${CMSIS_INCLUDE_DIRS} ${USBPD_INCLUDE_DIRS})

target_compile_definitions(charger-module PRIVATE
    STM32G071xx
    USER_VECT_TAB_ADDRESS # SystemInit points VTOR at the flash: the ROM bootloader may have started us
    ${FW_VERSION_DEFINITIONS}
    ${USBPD_DEFINITIONS}
)
# ST's sources are built as they come: their warnings are not ours to fix
set_source_files_properties(${USBPD_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(charger-module PRIVATE ${USBPD_CORE_LIBRARY})
if(BLADE_WATCHDOG)
    target_compile_definitions(charger-module PRIVATE BLADE_WATCHDOG=1)
endif()

set(CPU_FLAGS -mcpu=cortex-m0plus -mthumb)
target_compile_options(charger-module PRIVATE
    ${CPU_FLAGS}
    -Os -g3
    -ffunction-sections -fdata-sections -fno-common
    -Wall -Wextra
)
target_link_options(charger-module PRIVATE
    ${CPU_FLAGS}
    --specs=nano.specs --specs=nosys.specs -nostartfiles
    -T${CMAKE_CURRENT_LIST_DIR}/ld/stm32g071x8.ld
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/charger-module.map
    -Wl,--print-memory-usage
)
set_target_properties(charger-module PROPERTIES
    LINK_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/ld/stm32g071x8.ld)

add_custom_command(TARGET charger-module POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:charger-module>
            ${CMAKE_CURRENT_BINARY_DIR}/charger-module.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:charger-module>
            ${CMAKE_CURRENT_BINARY_DIR}/charger-module.hex
    VERBATIM)
