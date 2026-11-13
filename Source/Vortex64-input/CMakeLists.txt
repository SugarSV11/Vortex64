cmake_minimum_required(VERSION 3.16)
project(Vortex64-input CXX)

set(P64I_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/Vortex64-input)
file(MAKE_DIRECTORY ${P64I_GENERATED_DIR})
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Version.h.in ${P64I_GENERATED_DIR}/Version.h COPYONLY)

add_library(Vortex64-input SHARED
    CProject64Input.cpp
    InputMain.cpp
    InputSettings.cpp
    SdlInputBackend.cpp
    Win32KeyboardScan_Linux.cpp
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

target_include_directories(Vortex64-input PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(Vortex64-input PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SDL2_INCLUDE_DIRS}
    ${P64I_GENERATED_DIR}
)

set_target_properties(Vortex64-input PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

target_link_libraries(Vortex64-input PRIVATE
    Common
    Settings
    ${SDL2_LIBRARIES}
)
target_link_directories(Vortex64-input PRIVATE ${SDL2_LIBRARY_DIRS})
target_compile_options(Vortex64-input PRIVATE ${SDL2_CFLAGS_OTHER})

# Qt6 configuration dialog (ConfigQt.cpp), loaded on demand by DllConfig().
find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
    set(CMAKE_AUTOMOC OFF)
    add_library(Vortex64-input-qtconfig MODULE ConfigQt.cpp N64ControllerWidget.cpp ConfigQt.qrc)
    set_target_properties(Vortex64-input-qtconfig PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON AUTORCC ON
        PREFIX "lib" OUTPUT_NAME "Vortex64-input-qtconfig"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        INSTALL_RPATH "$ORIGIN" BUILD_RPATH "$ORIGIN")
    target_compile_definitions(Vortex64-input-qtconfig PRIVATE QT_NO_KEYWORDS)
    target_include_directories(Vortex64-input-qtconfig PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${SDL2_INCLUDE_DIRS}
        ${P64I_GENERATED_DIR})
    target_link_libraries(Vortex64-input-qtconfig PRIVATE Vortex64-input Common Settings Qt6::Widgets)
endif()
