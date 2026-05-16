cmake_minimum_required(VERSION 4.0.2)

project(Entry)
set(CMAKE_CXX_STANDARD 17)

set(ENTRY_HEADERS
    include/main.hpp
)

set(ENTRY_SOURCES
    src/main.cpp
)

find_package(spdlog CONFIG REQUIRED)
add_executable(Entry ${ENTRY_HEADERS} ${ENTRY_SOURCES})

target_include_directories(Entry
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(Entry PUBLIC STNKS_CORE)
