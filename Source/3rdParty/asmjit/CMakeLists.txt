cmake_minimum_required(VERSION 3.16)
project(asmjit CXX)

# Vendored asmjit 1.9.0 (MIT license, see LICENSE.md). Upstream ships its own
# CMakeLists.txt; this repository only vendors the sources plus a .vcxproj,
# so this file reproduces the "build everything, let ASMJIT_NO_* macros in
# api-config.h decide what's compiled in" behavior the .vcxproj relied on.
file(GLOB_RECURSE ASMJIT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asmjit/core/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asmjit/x86/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/asmjit/arm/*.cpp
)

add_library(asmjit STATIC ${ASMJIT_SOURCES})
target_include_directories(asmjit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(asmjit PUBLIC ASMJIT_STATIC)
set_target_properties(asmjit PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(asmjit PUBLIC Threads::Threads)
