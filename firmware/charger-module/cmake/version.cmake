# FW_VERSION "x.y.z" out of src/blade.h (release-please keeps it) into
# FW_MAJOR / FW_MINOR / FW_PATCH, for the identity registers.
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/../src/blade.h _fw_version_line REGEX "#define FW_VERSION")
if(NOT _fw_version_line MATCHES "\"([0-9]+)\\.([0-9]+)\\.([0-9]+)\"")
    message(FATAL_ERROR "cannot parse FW_VERSION out of src/blade.h")
endif()
set(FW_VERSION_DEFINITIONS
    FW_MAJOR=${CMAKE_MATCH_1}
    FW_MINOR=${CMAKE_MATCH_2}
    FW_PATCH=${CMAKE_MATCH_3}
)
set(FW_VERSION_STRING "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
