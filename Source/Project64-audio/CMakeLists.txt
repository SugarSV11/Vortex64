cmake_minimum_required(VERSION 3.16)
project(Project64-audio CXX)

# ---------------------------------------------------------------------------
# Linux-native build of the audio plugin.
#
# Upstream ships DirectSound (Windows) and OpenSLES (Android-only, guarded
# by #ifdef ANDROID; a no-op shell on any other platform) backends. Neither
# targets desktop Linux, so this tree adds Driver/SDLAudio.{h,cpp}, a third
# SoundDriverBase implementation built on SDL2's audio device callback API,
# and AudioMain.cpp picks it via #ifdef/#elif on non-Windows, non-Android
# builds.
#
# ConfigUI.cpp is a WTL/Win32 dialog for the volume/buffer settings UI and
# is excluded here; DllConfig() only calls into it under #ifdef _WIN32.
# ---------------------------------------------------------------------------

set(P64A_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/Project64-audio)
file(MAKE_DIRECTORY ${P64A_GENERATED_DIR})
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Version.h.in ${P64A_GENERATED_DIR}/Version.h COPYONLY)

add_library(Project64-audio SHARED
    AudioMain.cpp
    AudioSettings.cpp
    trace.cpp
    Driver/SoundBase.cpp
    Driver/SDLAudio.cpp
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

target_include_directories(Project64-audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(Project64-audio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SDL2_INCLUDE_DIRS}
    ${P64A_GENERATED_DIR}
)

set_target_properties(Project64-audio PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

target_link_libraries(Project64-audio PRIVATE
    Common
    Settings
    ${SDL2_LIBRARIES}
)
target_link_directories(Project64-audio PRIVATE ${SDL2_LIBRARY_DIRS})
target_compile_options(Project64-audio PRIVATE ${SDL2_CFLAGS_OTHER})

# Qt6 configuration dialog (ConfigQt.cpp), loaded on demand by DllConfig().
find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
    set(CMAKE_AUTOMOC OFF)
    add_library(Project64-audio-qtconfig MODULE ConfigQt.cpp)
    set_target_properties(Project64-audio-qtconfig PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        PREFIX "lib" OUTPUT_NAME "Project64-audio-qtconfig"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        INSTALL_RPATH "$ORIGIN" BUILD_RPATH "$ORIGIN")
    target_compile_definitions(Project64-audio-qtconfig PRIVATE QT_NO_KEYWORDS)
    target_include_directories(Project64-audio-qtconfig PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${P64A_GENERATED_DIR})
    target_link_libraries(Project64-audio-qtconfig PRIVATE Project64-audio Common Settings Qt6::Widgets)
endif()
