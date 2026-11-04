cmake_minimum_required(VERSION 3.16)
project(Settings CXX)

add_library(Settings STATIC
    Settings.cpp
)

target_include_directories(Settings PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
set_target_properties(Settings PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(Settings PUBLIC Common)
