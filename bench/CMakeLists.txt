cmake_minimum_required(VERSION 3.15)
project(steroidslog-benchmark)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-O3 -march=native -DNDEBUG)

include(${CMAKE_SOURCE_DIR}/cmake/GoogleBenchmark.cmake)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)

include(FetchContent)

# Single fmt for everyone
FetchContent_Declare(fmtlib
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 11.1.2)
FetchContent_MakeAvailable(fmtlib)

# spdlog: use external fmt
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "Use external fmt" FORCE)
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.15.3)
FetchContent_MakeAvailable(spdlog)

# quill
FetchContent_Declare(quill
  GIT_REPOSITORY https://github.com/odygrd/quill.git
  GIT_TAG v10.0.1)
FetchContent_MakeAvailable(quill)

# fmtlog: fetch sources ONLY
FetchContent_Declare(fmtlog
  GIT_REPOSITORY https://github.com/MengRao/fmtlog.git
  GIT_TAG v2.3.0)
FetchContent_GetProperties(fmtlog)
if(NOT fmtlog_POPULATED)
  FetchContent_Populate(fmtlog)  # get fmtlog.h
endif()

# Header-only interface for fmtlog
add_library(fmtlog_header_only INTERFACE)
target_include_directories(fmtlog_header_only INTERFACE ${fmtlog_SOURCE_DIR})
target_compile_definitions(fmtlog_header_only INTERFACE FMTLOG_HEADER_ONLY)
target_link_libraries(fmtlog_header_only INTERFACE fmt::fmt)

include_directories(${CMAKE_SOURCE_DIR}/lib)

add_executable(${PROJECT_NAME}
  logger_adapters.h
  figure5_bench.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  benchmark::benchmark
  steroidslog
  spdlog::spdlog
  quill::quill
  fmtlog_header_only
  fmt::fmt
)
