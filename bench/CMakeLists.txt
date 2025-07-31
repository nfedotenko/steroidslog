cmake_minimum_required(VERSION 3.15)

project(steroidslog-benchmark)

include(${CMAKE_SOURCE_DIR}/cmake/GoogleBenchmark.cmake)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-O3 -march=native -DNDEBUG)

include_directories(${CMAKE_SOURCE_DIR}/lib)

add_executable(${PROJECT_NAME} bench.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE
    benchmark::benchmark steroidslog
)
