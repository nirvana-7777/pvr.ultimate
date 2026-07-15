cmake_minimum_required(VERSION 3.5)
project(pvr.ultimate)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

# This will set RAPIDJSON_INCLUDE_DIRS
find_package(RapidJSON REQUIRED)
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
include_directories(
        ${KODI_INCLUDE_DIR}
        ${RAPIDJSON_INCLUDE_DIRS}
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

# If you need to pass flags to RapidJSON
add_definitions(-DRAPIDJSON_HAS_STDSTRING=1)
add_compile_options(-DRAPIDJSON_HAS_STDSTRING=1)

# NOTE: build_addon() is what actually creates the pvr.ultimate target (via
# ULTIMATE_SOURCES/ULTIMATE_HEADERS above). The target does not exist before
# this call, so any target_include_directories(pvr.ultimate ...) must come
# after it, not before -- calling it before would abort CMake configure
# with "target ... which is not built by this project" and prevent
# compile_commands.json from being (re)generated. The src/ include path is
# already covered by the include_directories() call above, so no extra
# target-scoped include is needed here.
build_addon(pvr.ultimate ULTIMATE DEPLIBS)

set_target_properties(pvr.ultimate PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON)

include(CPack)