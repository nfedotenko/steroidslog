cmake_minimum_required(VERSION 3.22)

project(steroidslog-benchmark)

add_compile_options(-O3 -DNDEBUG)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
include(${CMAKE_SOURCE_DIR}/cmake/GoogleBenchmark.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/BenchDependencies.cmake)

file(GLOB BENCH_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# Header-only interface for fmtlog
add_library(fmtlog_header_only INTERFACE)
target_include_directories(fmtlog_header_only INTERFACE ${fmtlog_SOURCE_DIR})
target_compile_definitions(fmtlog_header_only INTERFACE FMTLOG_HEADER_ONLY)
target_link_libraries(fmtlog_header_only INTERFACE fmt::fmt)

add_executable(${PROJECT_NAME} ${BENCH_SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    benchmark::benchmark
    steroidslog
    spdlog::spdlog
    quill::quill
    fmtlog_header_only
    fmt::fmt)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
