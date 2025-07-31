cmake_minimum_required(VERSION 3.15)

project(steroidslog)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS 
    "${CMAKE_CXX_FLAGS} -Wall -Wextra -fPIC"
)

file(GLOB_RECURSE LIB_HEADERS
    misc/*.h
    steroidslog/*.h
)

file(GLOB_RECURSE LIB_SOURCES
    misc/*.cpp
    steroidslog/*.cpp
)

add_library(${PROJECT_NAME} STATIC ${LIB_HEADERS} ${LIB_SOURCES})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/lib)
