cmake_minimum_required(VERSION 3.15)

project(steroidslog-tests)

include(${CMAKE_SOURCE_DIR}/cmake/GoogleTest.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS
    "${CMAKE_CXX_FLAGS} -Wall -Wextra -ftrapv"
)

include_directories(${CMAKE_SOURCE_DIR}/lib)

add_executable(${PROJECT_NAME} tests.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE
    gtest gtest_main steroidslog
)
