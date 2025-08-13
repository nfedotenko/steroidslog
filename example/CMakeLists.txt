cmake_minimum_required(VERSION 3.15)

project(steroidslog-example)

include_directories(${CMAKE_SOURCE_DIR}/lib)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE steroidslog)
