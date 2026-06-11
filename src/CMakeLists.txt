cmake_minimum_required(VERSION 3.20)

# VCPKG — toolchain must be set BEFORE project()
# Set VCPKG_ROOT via environment variable or pass -DVCPKG_ROOT=... to cmake
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED VCPKG_ROOT)
    set(VCPKG_ROOT "$ENV{VCPKG_ROOT}")
endif()

if(VCPKG_ROOT AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(STNKS)
set(CMAKE_CXX_STANDARD 17)

if(CMAKE_TOOLCHAIN_FILE)
    message(STATUS "VCPKG READY; TOOLCHAIN = [${CMAKE_TOOLCHAIN_FILE}]")
else()
    message(WARNING "No vcpkg toolchain found. Set VCPKG_ROOT env var or pass -DVCPKG_ROOT=<path>")
endif()

option(STNKS_HEADLESS "Build server-only without graphics (SDL2, OpenGL, ImGui)" OFF)

add_subdirectory(Core)

if(NOT STNKS_HEADLESS)
    message(STATUS "Compiling stnks (UI)")
    add_subdirectory(Entry)
endif()

message(STATUS "Compiling stnks-server (headless)")
add_subdirectory(Server)

if(NOT STNKS_HEADLESS)
    message(STATUS "Compiling stnks-tests")
    enable_testing()
    add_subdirectory(Tests)
endif()
