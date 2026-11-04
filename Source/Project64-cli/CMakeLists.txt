cmake_minimum_required(VERSION 3.16)
project(Project64-cli CXX)

# Minimal CLI frontend: no GUI, just enough to drive the core + plugins end
# to end. See main.cpp for why this is useful on its own, ahead of the
# Qt6 GUI.
add_executable(project64 main.cpp LinuxNotification.cpp)

target_include_directories(project64 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(project64 PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(project64 PRIVATE
    Common
    Project64-core
)

# Make sure the plugins this binary looks for next to itself (see main.cpp)
# are actually built before it, and land in the same output directory.
add_dependencies(project64 Project64-video Project64-rsp Project64-audio Project64-input)
set_target_properties(project64 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
set_target_properties(Project64-video Project64-rsp Project64-audio Project64-input PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
