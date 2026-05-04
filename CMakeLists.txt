cmake_minimum_required(VERSION 3.20)
project(SysY2022Compiler VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -Wall -Wextra -Wpedantic")

set(ANTLR4_TAG "4.13.1")

include(ExternalProject)

ExternalProject_Add(
    antlr4
    GIT_REPOSITORY https://github.com/antlr/antlr4.git
    GIT_TAG ${ANTLR4_TAG}
    SOURCE_DIR ${CMAKE_BINARY_DIR}/antlr4-src
    BINARY_DIR ${CMAKE_BINARY_DIR}/antlr4-build
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DANTLR4_WITH_STATIC_CRT=OFF
    INSTALL_COMMAND ""
    BUILD_COMMAND "make"
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_BUILD ON
)

ExternalProject_Get_Property(antlr4 SOURCE_DIR)
ExternalProject_Get_Property(antlr4 BINARY_DIR)

set(ANTLR4_INCLUDE_DIR
    ${SOURCE_DIR}/runtime/Cpp/runtime/src
    ${BINARY_DIR}/runtime/Cpp
)

set(ANTLR4_LIB_DIR
    ${BINARY_DIR}/runtime/Cpp/runtime
)

set(ANTLR4_LIBS
    antlr4_static
)

file(GLOB_RECURSE SOURCES
    src/*.cpp
    src/*/*.cpp
)

include_directories(
    include
    ${ANTLR4_INCLUDE_DIR}
)

link_directories(
    ${ANTLR4_LIB_DIR}
)

add_executable(sysyc
    ${SOURCES}
)

target_link_libraries(sysyc
    ${ANTLR4_LIBS}
)

add_dependencies(sysyc antlr4)

file(GLOB_RECURSE TEST_FILES
    test/*.sy
)

enable_testing()
