cmake_minimum_required(VERSION 3.22)

project(steroidslog-tests)

# CRT consistency for MSVC
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

include(${CMAKE_SOURCE_DIR}/cmake/GoogleTest.cmake)

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_executable(${PROJECT_NAME} ${TEST_SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    steroidslog
    GTest::gtest_main)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

# Let CTest discover individual TEST() cases at build time
include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DISCOVERY_TIMEOUT 60)
