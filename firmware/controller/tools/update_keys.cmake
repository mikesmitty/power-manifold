# Builds the update signing keys (keys/*.pem, Ed25519 public keys) into
# <target> as the generated update_keys.h. See keys/README.md.
function(pwrman_update_keys target controller_dir)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    file(GLOB _keys CONFIGURE_DEPENDS ${controller_dir}/keys/*.pem)
    set(_dir ${CMAKE_CURRENT_BINARY_DIR}/update_keys)
    add_custom_command(
        OUTPUT ${_dir}/update_keys.h
        COMMAND ${Python3_EXECUTABLE} ${controller_dir}/tools/update_keys.py
                --out ${_dir}/update_keys.h ${_keys}
        DEPENDS ${controller_dir}/tools/update_keys.py ${_keys}
        VERBATIM)
    target_sources(${target} PRIVATE ${_dir}/update_keys.h)
    target_include_directories(${target} PRIVATE ${_dir})
    list(LENGTH _keys _count)
    if(_count)
        message(STATUS "Update signing keys built in: ${_count}")
    else()
        message(WARNING "No update signing keys (keys/*.pem): this build installs unsigned images from the network")
    endif()
endfunction()
