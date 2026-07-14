cmake_minimum_required(VERSION 3.5)
project(pvr.ultimate)

# Generate compile_commands.json for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

# This will set RAPIDJSON_INCLUDE_DIRS
find_package(RapidJSON REQUIRED)
find_package(Kodi REQUIRED)

# Explicitly set include directories
include_directories(
        ${KODI_INCLUDE_DIR}/..
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

# Make sure the source directory is in the include path for the build
target_include_directories(pvr.ultimate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

build_addon(pvr.ultimate ULTIMATE DEPLIBS)

include(CPack)