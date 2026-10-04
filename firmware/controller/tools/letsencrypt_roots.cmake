# Builds the Let's Encrypt roots (roots/*.pem, X.509 CA certificates) into
# <target> as the generated letsencrypt_roots.h. See roots/README.md.
function(pwrman_letsencrypt_roots target controller_dir)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    file(GLOB _roots CONFIGURE_DEPENDS ${controller_dir}/roots/*.pem)
    set(_dir ${CMAKE_CURRENT_BINARY_DIR}/letsencrypt_roots)
    add_custom_command(
        OUTPUT ${_dir}/letsencrypt_roots.h
        COMMAND ${Python3_EXECUTABLE} ${controller_dir}/tools/letsencrypt_roots.py
                --out ${_dir}/letsencrypt_roots.h ${_roots}
        DEPENDS ${controller_dir}/tools/letsencrypt_roots.py ${_roots}
        VERBATIM)
    target_sources(${target} PRIVATE ${_dir}/letsencrypt_roots.h)
    target_include_directories(${target} PRIVATE ${_dir})
    list(LENGTH _roots _count)
    if(_count)
        message(STATUS "Let's Encrypt roots built in: ${_count}")
    else()
        message(WARNING "No Let's Encrypt roots (roots/*.pem): a verified broker link needs an installed certificate")
    endif()
endfunction()
