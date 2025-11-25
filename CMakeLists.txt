cmake_minimum_required(VERSION 3.5)
project(pvr.ultimate)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Kodi REQUIRED)

# Use PkgConfig to find jsoncpp
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP REQUIRED jsoncpp)

include_directories(${KODI_INCLUDE_DIR}/.. # Hack way with "/..", need bigger Kodi cmake rework to match right include ways
        ${JSONCPP_INCLUDE_DIRS})

set(DEPLIBS ${JSONCPP_LIBRARIES})

# Add library directories if needed
link_directories(${JSONCPP_LIBRARY_DIRS})

set(ULTIMATE_SOURCES src/PVRUltimate.cpp)

set(ULTIMATE_HEADERS src/PVRUltimate.h)

build_addon(pvr.ultimate ULTIMATE DEPLIBS)

include(CPack)