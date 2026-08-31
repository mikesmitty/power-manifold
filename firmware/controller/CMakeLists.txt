cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(power_manifold_controller C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

option(FAKE_BLADES "Simulate the backplane and blades; runs on a bare board" OFF)

if(FAKE_BLADES)
    set(DRIVER_SOURCES
        src/engine/sim/sim_blades.c
        src/engine/sim/sim_scenario.c
    )
else()
    set(DRIVER_SOURCES
        src/engine/mpq4242.c
        src/engine/ina226.c
        src/engine/tca9548a.c
        src/engine/tca9539.c
    )
endif()

add_executable(controller
    src/main.c
    src/ipc.c
    src/flash_map.c
    src/settings.c
    src/fault_log.c
    src/update.c
    src/update_image.c
    src/cli.c
    src/engine/engine.c
    src/engine/port_fsm.c
    src/engine/budget.c
    src/engine/leds.c
    src/net/net.c
    src/net/mqtt.c
    src/net/http.c
    ${DRIVER_SOURCES}
)

if(FAKE_BLADES)
    target_compile_definitions(controller PRIVATE PWRMAN_FAKE_BLADES=1)
endif()

pico_generate_pio_header(controller ${CMAKE_CURRENT_LIST_DIR}/src/engine/ws2812.pio)

target_include_directories(controller PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/engine # driver headers, for the sim seam
    ${CMAKE_CURRENT_LIST_DIR} # lwipopts.h
)

target_compile_definitions(controller PRIVATE
    PICO_STACK_SIZE=4096
    PICO_CORE1_STACK_SIZE=4096
)

target_link_libraries(controller
    pico_stdlib
    pico_multicore
    pico_flash
    boot_uf2_headers
    pico_unique_id
    hardware_i2c
    hardware_pio
    hardware_flash
    hardware_watchdog
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mqtt
    pico_lwip_mdns
    pico_lwip_sntp
)

pico_enable_stdio_usb(controller 1)
pico_enable_stdio_uart(controller 0)
pico_enable_stdio_rtt(controller 1)

# IMAGE_DEF version, used by the bootrom to pick between the A/B slots.
# FW_VERSION x.y.z maps to major=x, minor=y*256+z so the ROM's 32-bit
# (major<<16|minor) comparison stays ordered across releases.
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/src/manifold.h _fw_version_line REGEX "#define FW_VERSION")
if(NOT _fw_version_line MATCHES "\"([0-9]+)\\.([0-9]+)\\.([0-9]+)\"")
    message(FATAL_ERROR "cannot parse FW_VERSION out of src/manifold.h")
endif()
math(EXPR _fw_image_minor "${CMAKE_MATCH_2} * 256 + ${CMAKE_MATCH_3}")
pico_set_binary_version(controller MAJOR ${CMAKE_MATCH_1} MINOR ${_fw_image_minor})

pico_add_extra_outputs(controller)

# Partition table image (A/B app slots + data partition; flashed once per
# board, see README). The JSON is board-specific; firmware finds partitions
# by ID at runtime, so the app binary itself is layout-agnostic.
set(PARTITION_TABLE_JSON "${CMAKE_CURRENT_LIST_DIR}/partitions/pico2w-4mb.json"
    CACHE FILEPATH "Partition layout compiled into partition_table.uf2")
pico_init_picotool()
if(picotool_FOUND)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/partition_table.uf2
        COMMAND picotool partition create --quiet
                ${PARTITION_TABLE_JSON} ${CMAKE_CURRENT_BINARY_DIR}/partition_table.uf2
        DEPENDS ${PARTITION_TABLE_JSON}
        VERBATIM)
    add_custom_target(partition_table ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/partition_table.uf2)
endif()
