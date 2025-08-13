cmake_minimum_required(VERSION 3.22)

project(steroidslog)

file(GLOB_RECURSE STEROIDSLOG_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/steroidslog/*.cpp")

if(STEROIDSLOG_SRC)
    add_library(${PROJECT_NAME} STATIC ${STEROIDSLOG_SRC})

    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/lib)
else() # single header
    add_library(${PROJECT_NAME} INTERFACE)

    target_include_directories(${PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/lib>
        $<INSTALL_INTERFACE:lib>)
endif()

set(_warn_msvc "/W4;/permissive-;/EHsc;/utf-8")
set(_warn_gcc_clang "-Wall;-Wextra;-Wpedantic")

target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:${_warn_msvc}>
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:${_warn_gcc_clang}>)

target_compile_definitions(${PROJECT_NAME} PUBLIC
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN;NOMINMAX>)
