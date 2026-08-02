#.rst:
# Findnlohmann_json
# -----------
# Finds the nlohmann/json library
#
# This will define the following variables::
#
# NLOHMANN_JSON_FOUND - system has nlohmann/json
# NLOHMANN_JSON_INCLUDE_DIRS - the nlohmann/json include directory
#

if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_NLOHMANN_JSON nlohmann_json>=3.9.0 QUIET)
endif()

if(CORE_SYSTEM_NAME STREQUAL windows OR CORE_SYSTEM_NAME STREQUAL windowsstore)
    set(NLOHMANN_JSON_VERSION 3.11.3)
else()
    if(PC_NLOHMANN_JSON_VERSION)
        set(NLOHMANN_JSON_VERSION ${PC_NLOHMANN_JSON_VERSION})
    else()
        find_package(nlohmann_json 3.9.0 CONFIG REQUIRED QUIET)
        set(NLOHMANN_JSON_VERSION ${nlohmann_json_VERSION})
    endif()
endif()

find_path(NLOHMANN_JSON_INCLUDE_DIR NAMES nlohmann/json.hpp
        PATHS ${PC_NLOHMANN_JSON_INCLUDEDIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(nlohmann_json
        REQUIRED_VARS NLOHMANN_JSON_INCLUDE_DIR NLOHMANN_JSON_VERSION
        VERSION_VAR NLOHMANN_JSON_VERSION)

if(NLOHMANN_JSON_FOUND)
    set(NLOHMANN_JSON_INCLUDE_DIRS ${NLOHMANN_JSON_INCLUDE_DIR})
endif()

mark_as_advanced(NLOHMANN_JSON_INCLUDE_DIR)