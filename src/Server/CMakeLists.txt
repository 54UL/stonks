cmake_minimum_required(VERSION 3.20)

project(STNKS_SERVER CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(spdlog CONFIG REQUIRED)

add_executable(STNKS_SERVER src/main.cpp)

target_link_libraries(STNKS_SERVER PRIVATE STNKS_CORE)

set_target_properties(STNKS_SERVER PROPERTIES
    OUTPUT_NAME "stnks-server")
