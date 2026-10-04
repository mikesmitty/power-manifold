cmake_minimum_required(VERSION 3.13)

# the production controller card; any other board is named on the command line
set(PICO_BOARD pwrman_controller_card CACHE STRING "Board type")

# project board headers alongside the SDK's: wiznet_w6100_evb_pico2 and the
# production card, pwrman_controller_card (see boards/)
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
include(pico_sdk_import.cmake)

project(power_manifold_controller C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

option(FAKE_BLADES "Simulate the backplane and blades; runs on a bare board" OFF)
option(NET_WIFI "CYW43 WiFi + Improv BLE provisioning (Pico 2 W, RM2)" ON)
option(NET_ETH "WIZnet W6100 wired Ethernet on SPI0 GP16-21, probed at boot" ON)
option(CARRIER_INTERNAL_PULLUPS
    "Bench only: Pico 2 W on the pcie-breakout without the three 4.7k resistors; RP2350 internal pull-ups on SDA/SCL/ALERT# and a 100 kHz bus" OFF)
# The gen-3 blade firmware to carry (blade_bundle.h): the blade build's
# charger-module.bin. Defaults to a sibling build of firmware/charger-module
# when one is there; an empty value builds without an image, and such a
# controller starts blades that hold a good image but cannot program a blank one.
set(BLADE_IMAGE "" CACHE FILEPATH "Gen-3 blade firmware image (charger-module.bin) to bundle")
if(FAKE_BLADES)
    # the simulated blades keep two flash pages each: nothing to write an image into
    set(BLADE_IMAGE "")
elseif(NOT BLADE_IMAGE AND EXISTS ${CMAKE_CURRENT_LIST_DIR}/../charger-module/build/charger-module.bin)
    set(BLADE_IMAGE ${CMAKE_CURRENT_LIST_DIR}/../charger-module/build/charger-module.bin)
endif()

if(CARRIER_INTERNAL_PULLUPS AND PICO_BOARD STREQUAL "pwrman_controller_card")
    message(FATAL_ERROR "CARRIER_INTERNAL_PULLUPS is for the pcie-breakout carrier; the controller card has its own 4.7k pull-ups.")
endif()

if(NET_WIFI AND NOT PICO_CYW43_SUPPORTED)
    message(FATAL_ERROR "NET_WIFI=ON needs a board with a CYW43 radio (the controller card, a Pico 2 W). "
        "For a wired-only image (W6100-EVB-Pico2 etc.) configure with -DNET_WIFI=OFF.")
endif()

if(FAKE_BLADES)
    set(DRIVER_SOURCES
        src/engine/sim/sim_blades.c
        src/engine/sim/sim_inject.c
        src/engine/sim/sim_scenario.c
    )
else()
    set(DRIVER_SOURCES
        src/engine/mpq4242.c
        src/engine/ina226.c
        src/engine/blade3.c
        src/engine/stboot.c
        src/engine/tca9548a.c
        src/engine/tca9539.c
        src/engine/i2c_diag.c
    )
endif()

add_executable(controller
    src/main.c
    src/ipc.c
    src/flash_map.c
    src/stack_probe.c
    src/fault_trap.c
    src/boot_reason.c
    src/boot_reason_hw.c
    src/civil_time.c
    src/event_kind.c
    src/fault_text.c
    src/health.c
    src/ip4_text.c
    src/led_sched.c
    src/log_ring.c
    src/log_sink.c
    src/settings_json.c
    src/settings_util.c
    src/settings.c
    src/settings_hw_pico.c
    src/fault_log.c
    src/update.c
    src/update_image.c
    src/update_sig.c
    src/update_latest.c
    lib/monocypher/monocypher.c
    lib/monocypher/monocypher-ed25519.c
    src/cli.c
    src/engine/engine.c
    src/engine/fan_policy.c
    src/engine/port_fsm.c
    src/engine/blade.c
    src/engine/blade_bundle.c
    src/engine/blade_update.c
    src/engine/budget.c
    src/engine/leds.c
    src/engine/led_pattern.c
    src/net/net.c
    src/net/mqtt.c
    src/net/http.c
    src/net/jsonlite.c
    src/net/ota_pull.c
    src/net/http_url.c
    src/net/update_check.c
    src/ups/lad_proto.c
    src/ups/ups.c
    src/ups/lad_hw_pico.c
    src/bus_cap.c
    src/vin.c
    src/vin_hw_pico.c
    src/button.c
    src/button_hw_pico.c
    ${DRIVER_SOURCES}
)

if(FAKE_BLADES)
    # two flash pages per simulated blade: the header and a little "code"
    target_compile_definitions(controller PRIVATE PWRMAN_FAKE_BLADES=1 SIM_FLASH_SIZE=4096)
endif()
if(BLADE_IMAGE)
    get_filename_component(BLADE_IMAGE ${BLADE_IMAGE} ABSOLUTE)
    if(NOT EXISTS ${BLADE_IMAGE})
        message(FATAL_ERROR "BLADE_IMAGE ${BLADE_IMAGE} does not exist (build firmware/charger-module first, or pass an empty value)")
    endif()
    message(STATUS "Bundling gen-3 blade firmware ${BLADE_IMAGE}")
    target_sources(controller PRIVATE src/engine/blade_bundle.S)
    set_source_files_properties(src/engine/blade_bundle.S PROPERTIES
        COMPILE_DEFINITIONS "BLADE_BUNDLE_PATH=\"${BLADE_IMAGE}\""
        OBJECT_DEPENDS ${BLADE_IMAGE})
    target_compile_definitions(controller PRIVATE PWRMAN_BLADE_BUNDLE=1)
else()
    message(STATUS "No gen-3 blade firmware bundled (BLADE_IMAGE unset)")
endif()
if(CARRIER_INTERNAL_PULLUPS)
    target_compile_definitions(controller PRIVATE PWRMAN_INTERNAL_PULLUPS=1)
endif()

# Update signing: the public keys in keys/*.pem are built in, and an image
# then has to be signed by one of them to be installed over the network.
include(${CMAKE_CURRENT_LIST_DIR}/tools/update_keys.cmake)
pwrman_update_keys(controller ${CMAKE_CURRENT_LIST_DIR})

pico_generate_pio_header(controller ${CMAKE_CURRENT_LIST_DIR}/src/engine/ws2812.pio)

if(NET_WIFI)
    target_sources(controller PRIVATE
        src/net/improv.c
        src/net/improv_proto.c
        src/net/btstack_glue.c
    )
    target_compile_definitions(controller PRIVATE PWRMAN_NET_WIFI=1)
    # Improv Wi-Fi GATT database -> improv_profile.h (BTstack's compile_gatt.py)
    pico_btstack_make_gatt_header(controller PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/net/improv_profile.gatt)
    target_link_libraries(controller
        pico_cyw43_arch_lwip_threadsafe_background
        # BLE (Improv provisioning). Deliberately NOT pico_btstack_cyw43: its
        # flash-backed TLV store would format sectors at the top of flash; the
        # glue it provides is reimplemented without that in src/net/btstack_glue.c
        pico_btstack_ble
        pico_btstack_hci_transport_cyw43
        pico_btstack_run_loop_async_context
    )
else()
    # lwIP without the cyw43 arch: same NO_SYS + threadsafe-background shape
    target_link_libraries(controller
        pico_lwip_nosys
        pico_async_context_threadsafe_background
    )
endif()

if(NET_ETH)
    target_sources(controller PRIVATE
        src/net/w6100.c
        src/net/w6100_hw_pico.c
        src/net/eth.c
    )
    target_compile_definitions(controller PRIVATE PWRMAN_NET_ETH=1)
    target_link_libraries(controller hardware_spi)
endif()

target_include_directories(controller PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/engine # driver headers, for the sim seam
    ${CMAKE_CURRENT_LIST_DIR}/../charger-module/include # blade_regs.h: the gen-3 blade's register map, shared with its firmware
    ${CMAKE_CURRENT_LIST_DIR} # lwipopts.h, btstack_config.h
    ${CMAKE_CURRENT_LIST_DIR}/lib/monocypher # Ed25519 + SHA-512 for signed updates
)

target_link_libraries(controller pico_rand) # setup secret for the web UI

target_compile_definitions(controller PRIVATE
    PICO_STACK_SIZE=16384
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
    hardware_adc
    pico_lwip_mqtt
    pico_lwip_mdns
    pico_lwip_sntp
    pico_lwip_http # http_client for OTA pull (httpd side goes unused)
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

# core 0 stack in main SRAM (see memmap_bigstack.ld)
pico_set_linker_script(controller ${CMAKE_CURRENT_LIST_DIR}/memmap_bigstack.ld)

pico_add_extra_outputs(controller)

# Partition table image (A/B app slots + data partition; flashed once per
# board, see README). The JSON is board-specific; firmware finds partitions
# by ID at runtime, so the app binary itself is layout-agnostic. The
# controller card carries 16 MB; everything else here is a 4 MB Pico 2 W.
if(PICO_BOARD STREQUAL "pwrman_controller_card")
    set(_partition_default "${CMAKE_CURRENT_LIST_DIR}/partitions/prod-16mb.json")
else()
    set(_partition_default "${CMAKE_CURRENT_LIST_DIR}/partitions/pico2w-4mb.json")
endif()
set(PARTITION_TABLE_JSON "${_partition_default}"
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

    # controller-factory.bin: what a blank board is first programmed with
    # over SWD, written at 0x10000000 — the partition table and the firmware
    # in image slot A as one file (tools/factory_image.py, README "First
    # programming").
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/controller-factory.bin
        COMMAND picotool partition create --quiet
                ${PARTITION_TABLE_JSON} ${CMAKE_CURRENT_BINARY_DIR}/partition_table.bin
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/factory_image.py
                --layout ${PARTITION_TABLE_JSON}
                --table ${CMAKE_CURRENT_BINARY_DIR}/partition_table.bin
                --image ${CMAKE_CURRENT_BINARY_DIR}/controller.bin
                --out ${CMAKE_CURRENT_BINARY_DIR}/controller-factory.bin
        DEPENDS controller ${PARTITION_TABLE_JSON} ${CMAKE_CURRENT_LIST_DIR}/tools/factory_image.py
        VERBATIM)
    add_custom_target(factory_image ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/controller-factory.bin)
endif()
