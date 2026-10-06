# Builds web/index.html into <target> as the generated web_index.h, the page
# served at / (src/net/http.c). See tools/web_page.py.
function(pwrman_web_page target controller_dir)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_dir ${CMAKE_CURRENT_BINARY_DIR}/web_page)
    add_custom_command(
        OUTPUT ${_dir}/web_index.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_dir}
        COMMAND ${Python3_EXECUTABLE} ${controller_dir}/tools/web_page.py
                --out ${_dir}/web_index.h ${controller_dir}/web/index.html
        DEPENDS ${controller_dir}/tools/web_page.py ${controller_dir}/web/index.html
        VERBATIM)
    target_sources(${target} PRIVATE ${_dir}/web_index.h)
    target_include_directories(${target} PRIVATE ${_dir})
endfunction()
