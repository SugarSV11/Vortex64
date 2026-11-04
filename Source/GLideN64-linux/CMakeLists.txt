cmake_minimum_required(VERSION 3.16)
project(GLideN64-linux CXX C)

# ---------------------------------------------------------------------------
# GLideN64 (the submodule in ../GLideN64) as a Project64 video plugin on Linux.
#
# Upstream builds it for Windows (the Project64 plugin specification) or for Mupen64Plus, and its
# CMake refuses UNIX without MUPENPLUSAPI. The submodule is not modified: this builds its
# platform independent sources and the Zilmar API (ZilmarPluginAPI.cpp) with the files of this
# directory in place of the windows/ ones:
#   WindowsWGL_sdl.cpp          OpenGL context on the window of the emulator through SDL2
#   DisplayWindow_linux.cpp     the display window (the GUI sizes the window)
#   CommonAPIImpl_linux.cpp     InitiateGFX, XDG paths
#   MemoryStatus_linux.cpp, ScreenShot_linux.cpp, GLideN64_linux.cpp
#   fullscreenresolutions_linux.cpp   the modes of the settings dialog, from Qt
# overlay/ holds the two headers that the Windows sources include and that differ on Linux
# (they are found before the ones of the submodule).
# ---------------------------------------------------------------------------

set(GLN64 ${CMAKE_CURRENT_SOURCE_DIR}/../GLideN64/Source/src)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(FREETYPE REQUIRED freetype2)
find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Revision.h of getRevision.sh
set(GLN64_REVISION_DIR ${CMAKE_CURRENT_BINARY_DIR}/inc)
file(MAKE_DIRECTORY ${GLN64_REVISION_DIR})
execute_process(COMMAND git -C ${GLN64} rev-parse --short HEAD
    OUTPUT_VARIABLE GLN64_REV OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(GLN64_REV)
    file(WRITE ${GLN64_REVISION_DIR}/Revision.h
        "#define PLUGIN_REVISION \"${GLN64_REV}\"\n#define PLUGIN_REVISION_W L\"${GLN64_REV}\"\n")
else()
    file(WRITE ${GLN64_REVISION_DIR}/Revision.h "\n")
endif()

# winlnxdefs.h (Mupen64 types, what Platform.h includes outside Windows) has BOOL as unsigned int,
# the plugin specification declares the plugin functions with int. A copy of it with BOOL as int is
# included first (the original is guarded by the same macro and then skipped).
file(READ ${GLN64}/winlnxdefs.h GLN64_WINLNXDEFS)
string(REPLACE "typedef unsigned int BOOL, BOOLEAN;" "typedef int BOOL;\ntypedef unsigned int BOOLEAN;" GLN64_WINLNXDEFS "${GLN64_WINLNXDEFS}")
file(WRITE ${GLN64_REVISION_DIR}/pj64_winlnxdefs.h "${GLN64_WINLNXDEFS}")

set(GLN64_SOURCES
    ${GLN64}/3DMath.cpp
    ${GLN64}/Combiner.cpp
    ${GLN64}/CombinerKey.cpp
    ${GLN64}/CommonPluginAPI.cpp
    ${GLN64}/Config.cpp
    ${GLN64}/convert.cpp
    ${GLN64}/CRC32.cpp
    ${GLN64}/DebugDump.cpp
    ${GLN64}/Debugger.cpp
    ${GLN64}/DepthBuffer.cpp
    ${GLN64}/DisplayWindow.cpp
    ${GLN64}/DisplayLoadProgress.cpp
    ${GLN64}/FrameBuffer.cpp
    ${GLN64}/FrameBufferInfo.cpp
    ${GLN64}/GBI.cpp
    ${GLN64}/gDP.cpp
    ${GLN64}/GLideN64.cpp
    ${GLN64}/GraphicsDrawer.cpp
    ${GLN64}/gSP.cpp
    ${GLN64}/Log.cpp
    ${GLN64}/N64.cpp
    ${GLN64}/PaletteTexture.cpp
    ${GLN64}/Performance.cpp
    ${GLN64}/PostProcessor.cpp
    ${GLN64}/RDP.cpp
    ${GLN64}/RSP.cpp
    ${GLN64}/RSP_LoadMatrix.cpp
    ${GLN64}/SoftwareRender.cpp
    ${GLN64}/TexrectDrawer.cpp
    ${GLN64}/TextDrawer.cpp
    ${GLN64}/TextureFilterHandler.cpp
    ${GLN64}/Textures.cpp
    ${GLN64}/VI.cpp
    ${GLN64}/ZlutTexture.cpp
    ${GLN64}/BufferCopy/BlueNoiseTexture.cpp
    ${GLN64}/BufferCopy/ColorBufferToRDRAM.cpp
    ${GLN64}/BufferCopy/DepthBufferToRDRAM.cpp
    ${GLN64}/BufferCopy/RDRAMtoColorBuffer.cpp
    ${GLN64}/DepthBufferRender/ClipPolygon.cpp
    ${GLN64}/DepthBufferRender/DepthBufferRender.cpp
    ${GLN64}/common/CommonAPIImpl_common.cpp
    ${GLN64}/Graphics/Context.cpp
    ${GLN64}/Graphics/ColorBufferReader.cpp
    ${GLN64}/Graphics/CombinerProgram.cpp
    ${GLN64}/Graphics/ObjectHandle.cpp
    ${GLN64}/Graphics/OpenGLContext/GLFunctions.cpp
    ${GLN64}/Graphics/OpenGLContext/ThreadedOpenGl/opengl_Command.cpp
    ${GLN64}/Graphics/OpenGLContext/ThreadedOpenGl/opengl_ObjectPool.cpp
    ${GLN64}/Graphics/OpenGLContext/ThreadedOpenGl/opengl_Wrapper.cpp
    ${GLN64}/Graphics/OpenGLContext/ThreadedOpenGl/opengl_WrappedFunctions.cpp
    ${GLN64}/Graphics/OpenGLContext/ThreadedOpenGl/RingBufferPool.cpp
    ${GLN64}/Graphics/OpenGLContext/opengl_Attributes.cpp
    ${GLN64}/Graphics/OpenGLContext/opengl_BufferedDrawer.cpp
    ${GLN64}/Graphics/OpenGLContext/opengl_BufferManipulationObjectFactory.cpp
    ${GLN64}/Graphics/OpenGLContext/opengl_CachedFunctions.cpp
    ${GLN64}/Graphics/OpenGLContext/opengl_ColorBufferReaderWithBufferStorage.cpp
    ${GLN64}/Graphics/OpenGLContext/opengl_ColorBufferReaderWithPixelBuffer.cpp
    ${GLN64}/Graphics/OpenGLContext/opengl_ColorBufferReaderWithReadPixels.cpp
    ${GLN64}/Graphics/OpenGLContext/opengl_ContextImpl.cpp
    ${GLN64}/Graphics/OpenGLContext/opengl_GLInfo.cpp
    ${GLN64}/Graphics/OpenGLContext/opengl_Parameters.cpp
    ${GLN64}/Graphics/OpenGLContext/opengl_TextureManipulationObjectFactory.cpp
    ${GLN64}/Graphics/OpenGLContext/opengl_UnbufferedDrawer.cpp
    ${GLN64}/Graphics/OpenGLContext/opengl_Utils.cpp
    ${GLN64}/Graphics/OpenGLContext/GLSL/glsl_CombinerInputs.cpp
    ${GLN64}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramBuilder.cpp
    ${GLN64}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramBuilderCommon.cpp
    ${GLN64}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramBuilderAccurate.cpp
    ${GLN64}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramBuilderFast.cpp
    ${GLN64}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramImpl.cpp
    ${GLN64}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramUniformFactory.cpp
    ${GLN64}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramUniformFactoryCommon.cpp
    ${GLN64}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramUniformFactoryAccurate.cpp
    ${GLN64}/Graphics/OpenGLContext/GLSL/glsl_CombinerProgramUniformFactoryFast.cpp
    ${GLN64}/Graphics/OpenGLContext/GLSL/glsl_FXAA.cpp
    ${GLN64}/Graphics/OpenGLContext/GLSL/glsl_ShaderStorage.cpp
    ${GLN64}/Graphics/OpenGLContext/GLSL/glsl_SpecialShadersFactory.cpp
    ${GLN64}/Graphics/OpenGLContext/GLSL/glsl_Utils.cpp
    ${GLN64}/uCodes/F3D.cpp
    ${GLN64}/uCodes/F3DBETA.cpp
    ${GLN64}/uCodes/F3DDKR.cpp
    ${GLN64}/uCodes/F3DEX.cpp
    ${GLN64}/uCodes/F3DEX095.cpp
    ${GLN64}/uCodes/F3DAM.cpp
    ${GLN64}/uCodes/F3DEX2.cpp
    ${GLN64}/uCodes/F3DEX3.cpp
    ${GLN64}/uCodes/F3DEX2ACCLAIM.cpp
    ${GLN64}/uCodes/F3DEX2CBFD.cpp
    ${GLN64}/uCodes/F3DZEX2.cpp
    ${GLN64}/uCodes/F3DFLX2.cpp
    ${GLN64}/uCodes/F3DGOLDEN.cpp
    ${GLN64}/uCodes/F3DTEXA.cpp
    ${GLN64}/uCodes/F3DPD.cpp
    ${GLN64}/uCodes/F3DSETA.cpp
    ${GLN64}/uCodes/F5Indi_Naboo.cpp
    ${GLN64}/uCodes/F5Rogue.cpp
    ${GLN64}/uCodes/L3D.cpp
    ${GLN64}/uCodes/L3DEX2.cpp
    ${GLN64}/uCodes/L3DEX.cpp
    ${GLN64}/uCodes/S2DEX2.cpp
    ${GLN64}/uCodes/S2DEX.cpp
    ${GLN64}/uCodes/T3DUX.cpp
    ${GLN64}/uCodes/Turbo3D.cpp
    ${GLN64}/uCodes/ZSort.cpp
    ${GLN64}/uCodes/ZSortBOSS.cpp
    ${GLN64}/ZilmarPluginAPI.cpp
    Config_linux.cpp
    ZilmarAPIImpl_linux.cpp
    GLideN64_linux.cpp
    CommonAPIImpl_linux.cpp
    MemoryStatus_linux.cpp
    ScreenShot_linux.cpp
    DisplayWindow_linux.cpp
    WindowsWGL_sdl.cpp
    GLXSize.cpp
)
# the settings dialog (Qt)
set(GLN64_UI_SOURCES
    ${GLN64}/GLideNUI/AboutDialog.cpp
    ${GLN64}/GLideNUI/AboutDialog.ui
    ${GLN64}/GLideNUI/ConfigDialog.cpp
    ${GLN64}/GLideNUI/GLideNUI.cpp
    ${GLN64}/GLideNUI/Settings.cpp
    ${GLN64}/GLideNUI/QtKeyToHID.cpp
    ${GLN64}/GLideNUI/HIDKeyToName.cpp
    ${GLN64}/GLideNUI/configDialog.ui
    ${GLN64}/GLideNUI/icon.qrc
    fullscreenresolutions_linux.cpp
)
set(GLN64_HQ_SOURCES
    ${GLN64}/GLideNHQ/TextureFilters.cpp
    ${GLN64}/GLideNHQ/TextureFilters_2xsai.cpp
    ${GLN64}/GLideNHQ/TextureFilters_hq2x.cpp
    ${GLN64}/GLideNHQ/TextureFilters_hq4x.cpp
    ${GLN64}/GLideNHQ/TextureFilters_xbrz.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/patched/TxCache.cpp
    ${GLN64}/GLideNHQ/TxDbg.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/patched/TxFilter.cpp
    ${GLN64}/GLideNHQ/TxFilterExport.cpp
    ${GLN64}/GLideNHQ/TxHiResLoader.cpp
    ${GLN64}/GLideNHQ/TxHiResCache.cpp
    ${GLN64}/GLideNHQ/TxHiResNoCache.cpp
    ${GLN64}/GLideNHQ/TxImage.cpp
    ${GLN64}/GLideNHQ/TxQuantize.cpp
    ${GLN64}/GLideNHQ/TxReSample.cpp
    ${GLN64}/GLideNHQ/TxTexCache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/patched/TxUtil.cpp
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_library(GLideN64 SHARED ${GLN64_SOURCES} ${GLN64_UI_SOURCES} ${GLN64_HQ_SOURCES}
    ${GLN64}/osal/osal_files_unix.c ${GLN64}/osal/osal_keys_linux.c)
set_target_properties(GLideN64 PROPERTIES
    PREFIX "lib" OUTPUT_NAME "GLideN64"
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    INSTALL_RPATH "$ORIGIN" BUILD_RPATH "$ORIGIN")
target_compile_definitions(GLideN64 PRIVATE OS_LINUX TXFILTER_LIB NDEBUG)
# The full screen is the frontend's (it makes the render window full screen and the plugin follows), so the settings
# dialog is the one of the hosts of that kind (mupen64plus): no full screen / borderless / monitor / mode choices
# that would be stored and do nothing
set_source_files_properties(${GLN64}/GLideNUI/ConfigDialog.cpp PROPERTIES COMPILE_DEFINITIONS M64P_GLIDENUI)
target_compile_options(GLideN64 PRIVATE -pthread -w -include ${GLN64_REVISION_DIR}/pj64_winlnxdefs.h ${SDL2_CFLAGS_OTHER})
target_include_directories(GLideN64 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay ${CMAKE_CURRENT_SOURCE_DIR}/overlay/windows ${GLN64}/GLideNUI
    ${GLN64} ${GLN64}/inc ${GLN64}/osal ${GLN64}/GLideNHQ
    ${GLN64_REVISION_DIR}
    ${SDL2_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS}
    ${PNG_INCLUDE_DIRS})
target_link_directories(GLideN64 PRIVATE ${SDL2_LIBRARY_DIRS})
target_link_libraries(GLideN64 PRIVATE
    OpenGL::GL ${SDL2_LIBRARIES} ${FREETYPE_LIBRARIES} PNG::PNG ZLIB::ZLIB dl
    Qt6::Core Qt6::Gui Qt6::Widgets Threads::Threads)

# the files that the plugin reads next to it (or in ../share/vortex64/GLideN64)
add_custom_command(TARGET GLideN64 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../GLideN64/Source/ini/GLideN64.custom.ini
        ${CMAKE_BINARY_DIR}/bin/GLideN64.custom.ini
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/../GLideN64/Source/ini/GLideN64.ini
        ${CMAKE_BINARY_DIR}/bin/GLideN64.ini)

# next to the other plugins; the ini files are read from the directory of the plugin
include(GNUInstallDirs)
install(TARGETS GLideN64 LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/vortex64)
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../GLideN64/Source/ini/GLideN64.ini
    ${CMAKE_CURRENT_SOURCE_DIR}/../GLideN64/Source/ini/GLideN64.custom.ini
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/vortex64)

# The texture enhancement under AddressSanitizer (tests/txcache_test.cpp: the cache that evicts, the slices of xBRZ that must equal the whole
# image): with the files of Vortex64 it must pass; the "original" target is the same test with those of the submodule and must FAIL
# (use after free, seams), which shows that the test sees the faults
# the files of the submodule that Vortex64 patches (see the note at the top of each): the "original" variant uses the submodule's own
set(GLN64_HQ_PATCHED TxCache TxFilter TxUtil)
set(GLN64_HQ_NO_PATCHED ${GLN64_HQ_SOURCES})
foreach(TXC_NAME ${GLN64_HQ_PATCHED})
    list(REMOVE_ITEM GLN64_HQ_NO_PATCHED ${CMAKE_CURRENT_SOURCE_DIR}/patched/${TXC_NAME}.cpp)
endforeach()
foreach(TXC_VARIANT txcache-test txcache-original-test)
    set(TXC_PATCHED_FILES)
    foreach(TXC_NAME ${GLN64_HQ_PATCHED})
        if(TXC_VARIANT STREQUAL "txcache-test")
            list(APPEND TXC_PATCHED_FILES ${CMAKE_CURRENT_SOURCE_DIR}/patched/${TXC_NAME}.cpp)
        else()
            list(APPEND TXC_PATCHED_FILES ${GLN64}/GLideNHQ/${TXC_NAME}.cpp)
        endif()
    endforeach()
    add_executable(${TXC_VARIANT} EXCLUDE_FROM_ALL tests/txcache_test.cpp ${GLN64_HQ_NO_PATCHED} ${TXC_PATCHED_FILES} ${GLN64}/osal/osal_files_unix.c)
    target_include_directories(${TXC_VARIANT} PRIVATE ${GLN64} ${GLN64}/GLideNHQ ${GLN64}/GLideNHQ/inc ${GLN64}/osal)
    target_compile_definitions(${TXC_VARIANT} PRIVATE OS_LINUX NDEBUG)
    target_compile_options(${TXC_VARIANT} PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(${TXC_VARIANT} PRIVATE -fsanitize=address)
    target_link_libraries(${TXC_VARIANT} PRIVATE ZLIB::ZLIB PNG::PNG pthread)
    set_target_properties(${TXC_VARIANT} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endforeach()

# Every filter of the texture enhancement with several threads against the whole image (tests/txfilter_seams_test.cpp): with the TxFilter of
# Vortex64 it must pass; the "original" target uses the TxFilter of the submodule and must FAIL (seams at the cuts of the bands)
foreach(TXS_VARIANT txfilter-seams-test txfilter-seams-original-test)
    set(TXS_FILTER ${CMAKE_CURRENT_SOURCE_DIR}/patched/TxFilter.cpp)
    if(TXS_VARIANT STREQUAL "txfilter-seams-original-test")
        set(TXS_FILTER ${GLN64}/GLideNHQ/TxFilter.cpp)
    endif()
    set(TXS_FILES ${GLN64_HQ_SOURCES})
    list(REMOVE_ITEM TXS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/patched/TxFilter.cpp)
    add_executable(${TXS_VARIANT} EXCLUDE_FROM_ALL tests/txfilter_seams_test.cpp ${TXS_FILES} ${TXS_FILTER} ${GLN64}/osal/osal_files_unix.c)
    target_include_directories(${TXS_VARIANT} PRIVATE ${GLN64} ${GLN64}/GLideNHQ ${GLN64}/GLideNHQ/inc ${GLN64}/osal)
    target_compile_definitions(${TXS_VARIANT} PRIVATE OS_LINUX NDEBUG)
    target_compile_options(${TXS_VARIANT} PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(${TXS_VARIANT} PRIVATE -fsanitize=address)
    target_link_libraries(${TXS_VARIANT} PRIVATE ZLIB::ZLIB PNG::PNG pthread)
    set_target_properties(${TXS_VARIANT} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endforeach()
