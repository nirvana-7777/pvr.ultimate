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

include(CPack)