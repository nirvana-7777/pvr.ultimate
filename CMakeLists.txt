cmake_minimum_required(VERSION 3.5)
project(pvr.ultimate)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

# This will set RAPIDJSON_INCLUDE_DIRS
find_package(RapidJSON REQUIRED)

find_package(Kodi REQUIRED)

include_directories(${KODI_INCLUDE_DIR}/.. # Hack way with "/..", need bigger Kodi cmake rework to match right include ways
        ${RAPIDJSON_INCLUDE_DIRS})

set(ULTIMATE_SOURCES src/PVRUltimate.cpp)
set(ULTIMATE_HEADERS src/PVRUltimate.h)

addon_version(pvr.ultimate ULTIMATE)
add_definitions(-DULTIMATE_VERSION=${ULTIMATE_VERSION})

# If you need to pass flags to RapidJSON
add_definitions(-DRAPIDJSON_HAS_STDSTRING=1)
# Or compile flags
add_compile_options(-DRAPIDJSON_HAS_STDSTRING=1)

build_addon(pvr.ultimate ULTIMATE DEPLIBS)

include(CPack)