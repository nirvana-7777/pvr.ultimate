cmake_minimum_required(VERSION 3.5)
project(pvr.ultimate)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

# nlohmann/json via find_package(), NOT FetchContent. This addon is built
# through Kodi's own binary-addons bootstrap (cmake/addons), which has its
# own dependency-resolution machinery keyed off find_package(X REQUIRED)
# declarations in the addon's CMakeLists.txt - the CI workflow's separate
# "apt-get install rapidjson-dev" / "brew install rapidjson" / "vcpkg
# install rapidjson" steps exist specifically to satisfy that mechanism.
# FetchContent is invisible to it: switching to FetchContent here caused
# Kodi's own headers (kodi/addon-instance/PVR.h) to stop being found for
# every source file in CI, even though nothing about how Kodi itself is
# located was changed. find_package(nlohmann_json REQUIRED) matches the
# exact declaration style RapidJSON used successfully in this same
# pipeline, so the CI workflow's install steps must be updated in parallel
# (see the accompanying workflow YAML diff) - swap the rapidjson package
# for nlohmann-json's equivalent on each platform.
find_package(nlohmann_json REQUIRED)

find_package(Kodi REQUIRED)

message(STATUS "KODI_INCLUDE_DIR=${KODI_INCLUDE_DIR}")

file(GLOB_RECURSE _ultimate_kodi_headers "${KODI_INCLUDE_DIR}/*.h")
list(LENGTH _ultimate_kodi_headers _ultimate_kodi_header_count)
message(STATUS "Scanning ${_ultimate_kodi_header_count} Kodi headers under KODI_INCLUDE_DIR for PVR_SOURCE")

set(ULTIMATE_HAS_PVR_SOURCE FALSE)
foreach(_hdr ${_ultimate_kodi_headers})
    file(STRINGS "${_hdr}" _match REGEX "typedef enum PVR_SOURCE")
    if(_match)
        set(ULTIMATE_HAS_PVR_SOURCE TRUE)
        message(STATUS "Found PVR_SOURCE declaration in ${_hdr}")
    endif()
endforeach()

if(_ultimate_kodi_header_count EQUAL 0)
    message(WARNING "No Kodi headers found under KODI_INCLUDE_DIR - PVR_SOURCE detection cannot be trusted")
endif()

if(ULTIMATE_HAS_PVR_SOURCE)
    message(STATUS "Kodi PVR API declares PVR_SOURCE - using 3-arg GetChannelStreamProperties")
    add_definitions(-DULTIMATE_HAS_PVR_SOURCE=1)
else()
    message(STATUS "Kodi PVR API has no PVR_SOURCE - using legacy 2-arg GetChannelStreamProperties")
endif()

# Explicitly set include directories
# NOTE: nlohmann/json is consumed via the nlohmann_json::nlohmann_json
# imported target (linked below, after build_addon()), not via a raw
# include dir here - unlike RapidJSON, its CMake package doesn't expose an
# *_INCLUDE_DIRS variable, so there is nothing to add to this list for it.
include_directories(
        ${KODI_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# All source files
set(ULTIMATE_SOURCES
        src/PVRUltimate.cpp
        src/Utils.cpp
        src/ProviderManager.cpp
        src/ChannelManager.cpp
        src/EPGManager.cpp
        src/RecordingManager.cpp
        src/TimerManager.cpp
)

# All header files
set(ULTIMATE_HEADERS
        src/PVRUltimate.h
        src/Models.h
        src/Utils.h
        src/ProviderManager.h
        src/ChannelManager.h
        src/EPGManager.h
        src/RecordingManager.h
        src/TimerManager.h
)

addon_version(pvr.ultimate ULTIMATE)
add_definitions(-DULTIMATE_VERSION=${ULTIMATE_VERSION})

# NOTE: build_addon() is what actually creates the pvr.ultimate target (via
# ULTIMATE_SOURCES/ULTIMATE_HEADERS above). The target does not exist before
# this call, so any target_include_directories(pvr.ultimate ...) or
# target_link_libraries(pvr.ultimate ...) must come after it, not before --
# calling it before would abort CMake configure with "target ... which is
# not built by this project" and prevent compile_commands.json from being
# (re)generated. The src/ include path is already covered by the
# include_directories() call above, so no extra target-scoped include is
# needed here.
build_addon(pvr.ultimate ULTIMATE DEPLIBS)

# nlohmann/json is header-only but still needs to be linked as an imported
# target so its include directory (managed internally by the target,
# unlike RapidJSON's RAPIDJSON_INCLUDE_DIRS) is propagated to this build.
target_link_libraries(pvr.ultimate PRIVATE nlohmann_json::nlohmann_json)

set_target_properties(pvr.ultimate PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON)

include(CPack)