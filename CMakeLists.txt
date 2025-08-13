cmake_minimum_required(VERSION 3.22)

project(steroidslog-main LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

option(STEROIDSLOG_BUILD_EXAMPLE "Build example" OFF)
option(STEROIDSLOG_BUILD_BENCH   "Build benchmarks" OFF)

add_subdirectory(lib)

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

if(STEROIDSLOG_BUILD_BENCH)
  add_subdirectory(bench)
endif()

if(STEROIDSLOG_BUILD_EXAMPLE)
  add_subdirectory(example)
endif()
