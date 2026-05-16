cmake_minimum_required(VERSION 3.20)

project(STNKS_TESTS CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(GTest CONFIG REQUIRED)

set(TEST_SOURCES
    src/StrategyModelTests.cpp
    src/StrategyStoreTests.cpp
    src/StrategyActionTests.cpp
    src/StrategyServerTests.cpp
)

add_executable(STNKS_TESTS ${TEST_SOURCES})

target_link_libraries(STNKS_TESTS
    PRIVATE STNKS_CORE
    PRIVATE GTest::gtest
    PRIVATE GTest::gtest_main)

set_target_properties(STNKS_TESTS PROPERTIES
    OUTPUT_NAME "stnks-tests")

include(GoogleTest)
gtest_discover_tests(STNKS_TESTS)
