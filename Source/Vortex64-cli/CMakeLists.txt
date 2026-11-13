cmake_minimum_required(VERSION 3.16)
project(Vortex64-cli CXX)

# Minimal CLI frontend: no GUI, just enough to drive the core + plugins end
# to end. See main.cpp for why this is useful on its own, ahead of the
# Qt6 GUI.
add_executable(vortex64-cli main.cpp LinuxNotification.cpp)

target_include_directories(vortex64-cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(vortex64-cli PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(vortex64-cli PRIVATE
    Common
    Vortex64-core
)

# Make sure the plugins this binary looks for next to itself (see main.cpp)
# are actually built before it, and land in the same output directory.
add_dependencies(vortex64-cli Vortex64-video Vortex64-rsp Vortex64-audio Vortex64-input)
set_target_properties(vortex64-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
set_target_properties(Vortex64-video Vortex64-rsp Vortex64-audio Vortex64-input PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
