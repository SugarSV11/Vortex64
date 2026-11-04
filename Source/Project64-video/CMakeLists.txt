cmake_minimum_required(VERSION 3.16)
project(Project64-video CXX C)

# ---------------------------------------------------------------------------
# Linux-native build of the Glide64-derived video plugin.
#
# Upstream only ships this as a Visual Studio project (native Win32/WGL) plus
# an Android NDK build (Android/jni/Project64-video/CMakeLists.txt, GLESv2 +
# Android windowing). Neither targets desktop Linux/X11/Wayland.
#
# This tree mirrors the Windows .vcxproj source list but swaps:
#   - Renderer/OGL*.cpp   (desktop GL, WGL context bits guarded by _WIN32)
#     instead of Renderer/OGLES*.cpp (GLES2/Android)
#   - SDL2 for window + GL context creation instead of WGL (see the
#     `#else` branch of gfxSstWinOpen() in Renderer/OGLglitchmain.cpp)
#   - system libpng/zlib instead of the vendored Android copies
# ---------------------------------------------------------------------------

set(P64V_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/Project64-video)
file(MAKE_DIRECTORY ${P64V_GENERATED_DIR})
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Version.h.in ${P64V_GENERATED_DIR}/Version.h COPYONLY)

add_library(Project64-video SHARED
    3dmath.cpp
    Android.cpp
    Combine.cpp
    Config.cpp
    CRC.cpp
    Debugger.cpp
    DepthBufferRender.cpp
    F3DTEXA.cpp
    FBtoScreen.cpp
    Main.cpp
    rdp.cpp
    ScreenResolution.cpp
    Settings.cpp
    TexBuffer.cpp
    TexCache.cpp
    trace.cpp
    turbo3D.cpp
    ucode.cpp
    ucode00.cpp
    ucode01.cpp
    ucode02.cpp
    ucode03.cpp
    ucode04.cpp
    ucode05.cpp
    ucode06.cpp
    ucode07.cpp
    ucode08.cpp
    ucode09.cpp
    ucode09rdp.cpp
    ucodeFB.cpp
    Util.cpp
    Ext_TxFilter.cpp
    Renderer/OGLcombiner.cpp
    Renderer/OGLgeometry.cpp
    Renderer/OGLglitchmain.cpp
    Renderer/GLXDrawable.cpp
    Renderer/OGLtextures.cpp
    Renderer/Renderer.cpp
    TextureEnhancer/TxFilterExport.cpp
    TextureEnhancer/TxFilter.cpp
    TextureEnhancer/TxCache.cpp
    TextureEnhancer/TxTexCache.cpp
    TextureEnhancer/TxHiResCache.cpp
    TextureEnhancer/TxQuantize.cpp
    TextureEnhancer/TxUtil.cpp
    TextureEnhancer/TextureFilters.cpp
    TextureEnhancer/TextureFilters_2xsai.cpp
    TextureEnhancer/TextureFilters_hq2x.cpp
    TextureEnhancer/TextureFilters_hq4x.cpp
    TextureEnhancer/TxImage.cpp
    TextureEnhancer/TxReSample.cpp
    TextureEnhancer/TxDbg.cpp
    TextureEnhancer/tc-1.1+/fxt1.c
    TextureEnhancer/tc-1.1+/dxtn.c
    TextureEnhancer/tc-1.1+/wrapper.c
    TextureEnhancer/tc-1.1+/texstore.c
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)
# Prefer the legacy libGL.so.1 over GLVND's libOpenGL.so/libGLX.so: the
# renderer calls several pre-1.3 ARB compatibility entry points
# (glActiveTextureARB, glMultiTexCoord2fARB) directly rather than through
# glXGetProcAddress/SDL_GL_GetProcAddress, and GLVND's libOpenGL.so does not
# export those legacy ARB aliases, causing a dlopen() failure at runtime.
set(OpenGL_GL_PREFERENCE LEGACY)
find_package(OpenGL REQUIRED)

target_include_directories(Project64-video PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(Project64-video PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdParty
    ${SDL2_INCLUDE_DIRS}
    ${P64V_GENERATED_DIR}
)

set_target_properties(Project64-video PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

target_link_libraries(Project64-video PRIVATE
    Common
    Settings
    PNG::PNG
    ZLIB::ZLIB
    OpenGL::GL
    ${SDL2_LIBRARIES}
)
target_link_directories(Project64-video PRIVATE ${SDL2_LIBRARY_DIRS})
target_compile_options(Project64-video PRIVATE ${SDL2_CFLAGS_OTHER})

# Qt6 configuration dialog (ConfigQt.cpp), loaded on demand by DllConfig() so
# that the plugin itself does not depend on Qt. Skipped when Qt6 is missing.
find_package(Qt6 QUIET COMPONENTS Widgets)
if(Qt6_FOUND)
    set(CMAKE_AUTOMOC OFF)
    add_library(Project64-video-qtconfig MODULE ConfigQt.cpp)
    set_target_properties(Project64-video-qtconfig PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        PREFIX "lib" OUTPUT_NAME "Project64-video-qtconfig"
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        INSTALL_RPATH "$ORIGIN" BUILD_RPATH "$ORIGIN")
    target_compile_definitions(Project64-video-qtconfig PRIVATE QT_NO_KEYWORDS)
    target_include_directories(Project64-video-qtconfig PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../3rdParty
        ${P64V_GENERATED_DIR})
    # Linking against the plugin gives this module access to g_settings and
    # the CSettings methods (resolved against the copy the emulator loaded).
    target_link_libraries(Project64-video-qtconfig PRIVATE Project64-video Common Settings Qt6::Widgets)
endif()

# Glide64's texture loaders (rdp.cpp loadBlock: "v16 += dxt; if (v16 < 0)") rely on
# signed 32-bit overflow wrapping, which MSVC provides and GCC -O3 treats as
# undefined; without this every LoadBlock texture with odd rows to swap
# (HUD, "PRESS START", backgrounds) came out with a checker/stripe pattern.
target_compile_options(Project64-video PRIVATE -fwrapv)
