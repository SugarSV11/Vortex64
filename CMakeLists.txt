cmake_minimum_required(VERSION 3.16)
project(Vortex64 CXX C)

# ---------------------------------------------------------------------------
# Linux-native desktop build.
#
# This is a from-scratch CMake tree; the upstream project only ships a
# Visual Studio solution (Project64.sln, MSBuild-only, 80 .vcxproj) plus an
# Android NDK build (Android/jni/*/CMakeLists.txt, used here only as a
# cross-check for which sources belong to which module).
#
# Current stage: the emulator core, native SDL2/OpenGL video, audio, input,
# and RSP plugins, CLI frontend, and Qt6 GUI frontend all build and run
# natively on Linux.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT UNIX)
    message(FATAL_ERROR "This CMakeLists.txt targets Linux only. Use Project64.sln on Windows.")
endif()

# __FILE__, assertions and debug info must not carry the absolute path of whoever built the binaries
# (the user name and the folders of the machine): the source tree is "." and the build tree "build".
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        "-ffile-prefix-map=${CMAKE_SOURCE_DIR}=."
        "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build")
endif()

find_package(Threads REQUIRED)

add_subdirectory(Source/3rdParty/asmjit)
add_subdirectory(Source/3rdParty/softfloat-3e)
add_subdirectory(Source/3rdParty/7zip-decoder)
add_subdirectory(Source/3rdParty/zlib)
add_subdirectory(Source/3rdParty/discord-rpc)
add_subdirectory(Source/3rdParty/zlib/contrib/minizip)
add_subdirectory(Source/Common)
add_subdirectory(Source/Vortex64-rsp-core)
add_subdirectory(Source/Vortex64-core)
add_subdirectory(Source/Settings)
add_subdirectory(Source/Vortex64-video)
add_subdirectory(Source/Vortex64-rsp)
add_subdirectory(Source/Vortex64-audio)
add_subdirectory(Source/Vortex64-input)

# GLideN64 as an alternative video plugin (the submodule is not modified, see Source/GLideN64-linux)
option(PJ64_GLIDEN64 "Build GLideN64 as a video plugin" ON)
if(PJ64_GLIDEN64 AND EXISTS ${CMAKE_SOURCE_DIR}/Source/GLideN64/Source/src/Config.cpp)
    add_subdirectory(Source/GLideN64-linux)
endif()

# Frontends
add_subdirectory(Source/Vortex64-cli)
add_subdirectory(Source/Vortex64-gui)

# Ship the real RDB databases (ucode CRC table, per-game settings, etc.)
# next to the binary instead of leaving the runtime to create empty 0-byte
# stubs the first time it looks for Config/*.rdb and finds nothing there.
# Without this, CSettings::DetectUCode() can't recognize *any* game's
# microcode, which the plugin (rdp.cpp's microcheck()) treats as a hard
# "release the video context and stop" condition for every ROM.
add_custom_target(copy-config-rdb ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/Config
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/Config/Project64.rdb
        ${CMAKE_SOURCE_DIR}/Config/Video.rdb
        ${CMAKE_SOURCE_DIR}/Config/Audio.rdb
        ${CMAKE_BINARY_DIR}/bin/Config/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/Config/Project64.rdx
        ${CMAKE_BINARY_DIR}/bin/Config/
    # Stock cheats/enhancements (Cheats and Enhancements dialogs) and the
    # language files (File > Language)
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Config/Cheats ${CMAKE_BINARY_DIR}/bin/Config/Cheats
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Config/Enhancements ${CMAKE_BINARY_DIR}/bin/Config/Enhancements
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/Lang ${CMAKE_BINARY_DIR}/bin/Lang
    # Scripts of the debugger (copied to the user's Scripts directory on first use)
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin/Scripts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/Scripts/example.js ${CMAKE_SOURCE_DIR}/Scripts/api_documentation.js ${CMAKE_BINARY_DIR}/bin/Scripts/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/JS-API-Documentation.html ${CMAKE_BINARY_DIR}/bin/
    COMMENT "Copying Config/ databases, stock cheats/enhancements, Lang/ and Scripts/ next to the built binary"
)

# ---------------------------------------------------------------------------
# `cmake --install` — a real system install (e.g. `cmake --install build
# --prefix /usr/local`, or DESTDIR-staged for distro packaging), the
# foundation any further packaging (a .deb/.rpm, an AppImage, a Flatpak)
# would build on top of. Not itself an AppImage/Flatpak (those still need
# their own bundling of Qt6/SDL2/Mesa runtime deps, not attempted here).
#
# Everything (both frontends, the four plugin .so's, and the Config/*.rdb
# databases) installs together into ${CMAKE_INSTALL_LIBDIR}/vortex64/
# rather than scattering the plugins under a shared libdir: they're
# resolved at runtime relative to CPath::MODULE_DIRECTORY (see
# Vortex64-cli/main.cpp and Vortex64-gui/MainWindow.cpp), i.e. "next to
# whatever binary is actually running", the same relationship they have in
# the build tree's bin/. /usr/bin/vortex64[-cli] are then symlinks into
# that directory rather than copies, which works because CPath::Module()
# resolves via /proc/self/exe - a Linux symlink target, not the symlink
# itself, so MODULE_DIRECTORY still correctly resolves to
# ${CMAKE_INSTALL_LIBDIR}/vortex64/ even when launched via the /usr/bin
# symlink.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

set(P64_INSTALL_APPDIR ${CMAKE_INSTALL_LIBDIR}/vortex64)

install(TARGETS vortex64-cli vortex64
    RUNTIME DESTINATION ${P64_INSTALL_APPDIR}
)
install(TARGETS Vortex64-video Vortex64-rsp Vortex64-audio Vortex64-input
    LIBRARY DESTINATION ${P64_INSTALL_APPDIR}
)
foreach(qtconfig Vortex64-video-qtconfig Vortex64-audio-qtconfig Vortex64-input-qtconfig)
    if(TARGET ${qtconfig})
        install(TARGETS ${qtconfig} LIBRARY DESTINATION ${P64_INSTALL_APPDIR})
    endif()
endforeach()
install(FILES
    ${CMAKE_SOURCE_DIR}/Config/Project64.rdb
    ${CMAKE_SOURCE_DIR}/Config/Video.rdb
    ${CMAKE_SOURCE_DIR}/Config/Audio.rdb
    ${CMAKE_SOURCE_DIR}/Config/Project64.rdx
    DESTINATION ${P64_INSTALL_APPDIR}/Config
)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/Config/Cheats ${CMAKE_SOURCE_DIR}/Config/Enhancements
    DESTINATION ${P64_INSTALL_APPDIR}/Config
)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/Lang DESTINATION ${P64_INSTALL_APPDIR})
install(FILES ${CMAKE_SOURCE_DIR}/Scripts/example.js ${CMAKE_SOURCE_DIR}/Scripts/api_documentation.js
    DESTINATION ${P64_INSTALL_APPDIR}/Scripts
)
install(FILES ${CMAKE_SOURCE_DIR}/JS-API-Documentation.html DESTINATION ${P64_INSTALL_APPDIR})

foreach(P64_SYMLINK_NAME vortex64-cli vortex64)
    install(CODE "
        file(RELATIVE_PATH P64_REL_TARGET
            \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\"
            \"\${CMAKE_INSTALL_PREFIX}/${P64_INSTALL_APPDIR}/${P64_SYMLINK_NAME}\")
        file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
        file(CREATE_LINK \"\${P64_REL_TARGET}\"
            \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/${P64_SYMLINK_NAME}\"
            SYMBOLIC)
    ")
endforeach()

install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/vortex64.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
# the icons of Vortex64 (written by the vortex64-icongen target, the same drawing the application paints)
foreach(P64_ICON_SIZE 16 24 32 48 64 128 256 512)
    install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/icons/vortex64-${P64_ICON_SIZE}.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${P64_ICON_SIZE}x${P64_ICON_SIZE}/apps
        RENAME vortex64.png
    )
endforeach()
install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/icons/vortex64.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)
install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/vortex64-mime.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages
)

# ---------------------------------------------------------------------------
# Tests. The binaries are built by `cmake --build build --target check`; `ctest --test-dir build`
# then runs Scripts/linux/run-offscreen-tests.sh, the suite that knows the arguments of every test
# (headless: Qt offscreen, isolated XDG directories, header-only fake ROMs; no display, audio
# device, gamepad or real game needed). The ones that do need them (Scripts/linux/run-gui-tests.sh,
# input-gamepad-test, audio-test) are run by hand.
# ---------------------------------------------------------------------------
enable_testing()
set(VORTEX_TEST_TARGETS
    common-test migration-test first-run-test settings-test plugindir-test language-test layout-test
    rombrowser-test mainmenu-test vortex-test present-policy-test plugin-config-test
    controller-widget-test plugins-test input-capture-test input-ui-test)
set(VORTEX_EXISTING_TESTS)
foreach(VT ${VORTEX_TEST_TARGETS})
    if(TARGET ${VT})
        list(APPEND VORTEX_EXISTING_TESTS ${VT})
    endif()
endforeach()
add_custom_target(check DEPENDS ${VORTEX_EXISTING_TESTS})
add_test(NAME offscreen-suite
    COMMAND ${CMAKE_SOURCE_DIR}/Scripts/linux/ctest-offscreen.sh ${CMAKE_BINARY_DIR})
set_tests_properties(offscreen-suite PROPERTIES TIMEOUT 900)
