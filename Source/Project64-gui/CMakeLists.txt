cmake_minimum_required(VERSION 3.16)
project(Project64-gui CXX)

# Native Qt6 GUI frontend with ROM loading, runtime controls, thread-safe
# CNotification dispatch, and an X11/Xwayland native widget used by the SDL2
# video plugin. Settings dialogs, ROM browser, and debugger remain future
# work; see LINUX_PORT.md.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test DBus)
find_package(X11 REQUIRED)

# The Vortex64 frontend: theme, icons, store, library model and views, shell
# (what the settings window needs)
set(VORTEX_BASE_SOURCES
    Vortex/vortex.qrc
    Vortex/Theme.cpp
    Vortex/Icons.cpp
    Vortex/Store.cpp
    Vortex/Language.cpp
)
set(VORTEX_SOURCES
    ${VORTEX_BASE_SOURCES}
    Vortex/LibraryModel.cpp
    Vortex/Covers.cpp
    Vortex/GameBrowser.cpp
    Vortex/Sidebar.cpp
    Vortex/DetailPanel.cpp
    Vortex/Shell.cpp
    Vortex/InputBridge.cpp
    Vortex/PluginDirCheck.cpp
    Vortex/ShotScript.cpp
)

add_executable(project64-gui
    main.cpp
    MainWindow.cpp
    MainMenu.cpp
    ${VORTEX_SOURCES}
    VirtualKeys.cpp
    Settings/ShortCutsPage.cpp
    Dialogs/AboutDialog.cpp
    Dialogs/LogOptionsDialog.cpp
    ScreenSaverInhibit.cpp
    PresentSync.cpp
    Dialogs/FirstRun.cpp
    Dialogs/CheatDialog.cpp
    Dialogs/EnhancementDialog.cpp
    Dialogs/EnhancementTree.cpp
    about.qrc
    ../Project64/UserInterface/MenuShortCuts.cpp
    ../Project64/UserInterface/DiscordRPC.cpp
    QtNotification.cpp
    RomBrowser.cpp
    RomInformation.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64/Plugins/PluginList.cpp
    Settings/SettingsDialog.cpp
    Settings/SettingsPage.cpp
    Settings/SettingsPages.cpp
    Settings/VortexPages.cpp
    $<TARGET_OBJECTS:Project64-debugger-ui>
    $<TARGET_OBJECTS:Project64-debugger-backend>
    $<TARGET_OBJECTS:Project64-debugger-scripts>
)

# ${CMAKE_CURRENT_SOURCE_DIR} provides the stdafx.h shim used by the upstream
# Project64/Settings files compiled unchanged.
target_include_directories(project64-gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(project64-gui PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
# QtNotification.cpp pulls in N64System.h -> the Recompiler headers ->
# 3rdParty/asmjit, whose x86 emitter has real methods literally named
# emit() (CX64Ops::emit-style call sites like _emitter()->emit(...)). Qt's
# convenience "emit signalName();" macro is a textual #define emit that
# does a blind token replacement, so without this it turns
# "_emitter()->emit(...)" into "_emitter()->(...)" and fails to parse.
# This code never uses the emit/signals/slots/foreach keyword macros
# (Q_EMIT/Q_SIGNALS/Q_SLOTS would be used instead if it ever does), so
# disabling them is free.
target_compile_definitions(project64-gui PRIVATE QT_NO_KEYWORDS)
target_link_libraries(project64-gui PRIVATE
    Common
    Project64-core
    Qt6::Widgets
    Qt6::DBus
    X11::X11
    vendored_png
    discord_rpc
)

add_dependencies(project64-gui Project64-video Project64-rsp Project64-audio Project64-input)
# The product is Vortex64: its executable is vortex64 (the target keeps its old name, which the tests and scripts use)
set_target_properties(project64-gui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    OUTPUT_NAME vortex64
)

# Regression test for the Settings pages (offscreen Qt, isolated XDG dirs).
# Run: ctest -R gui-settings   (or execute the two "settings-test" steps by hand)
add_executable(settings-test EXCLUDE_FROM_ALL
    tests/settings_test.cpp
    ${VORTEX_BASE_SOURCES}
    RomBrowser.cpp
    Settings/SettingsDialog.cpp
    Settings/SettingsPage.cpp
    Settings/SettingsPages.cpp
    Settings/VortexPages.cpp
    PresentSync.cpp
    Settings/ShortCutsPage.cpp
    VirtualKeys.cpp
    ../Project64/UserInterface/MenuShortCuts.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64/Plugins/PluginList.cpp
    ../Project64-cli/LinuxNotification.cpp
)
target_include_directories(settings-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(settings-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(settings-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(settings-test PRIVATE Common Project64-core Qt6::Widgets)

# Free assignment of keys, mouse buttons and wheel in the input plugin's Qt dialog.
add_executable(input-capture-test EXCLUDE_FROM_ALL
    tests/input_capture_test.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64-cli/LinuxNotification.cpp
)
target_include_directories(input-capture-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(input-capture-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(input-capture-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(input-capture-test PRIVATE Common Project64-core Qt6::Widgets)
add_dependencies(input-capture-test Project64-input)
if(TARGET Project64-input-qtconfig)
    add_dependencies(input-capture-test Project64-input-qtconfig)
endif()

# The look and the states of the Input dialog (cards, capsules, drawing of the controller linked to the rows, test page)
add_executable(input-ui-test EXCLUDE_FROM_ALL
    tests/input_ui_test.cpp
    Vortex/Theme.cpp
    Vortex/Icons.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64-cli/LinuxNotification.cpp
)
target_include_directories(input-ui-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(input-ui-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(input-ui-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(input-ui-test PRIVATE Common Project64-core Qt6::Widgets)
add_dependencies(input-ui-test Project64-input)
if(TARGET Project64-input-qtconfig)
    add_dependencies(input-ui-test Project64-input-qtconfig)
endif()

# What the input dialog stored drives the N64 pad: SDL window + XTEST events + GetKeys of the plugin.
add_executable(input-e2e-test EXCLUDE_FROM_ALL
    tests/input_e2e_test.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64-cli/LinuxNotification.cpp
)
target_include_directories(input-e2e-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/.. /usr/include/SDL2)
set_target_properties(input-e2e-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(input-e2e-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(input-e2e-test PRIVATE Common Project64-core Qt6::Widgets SDL2 X11 :libXtst.so.6)
add_dependencies(input-e2e-test Project64-input)

# The input dialog with real X events (XTEST): the click on a row's button, then a key / a click.
add_executable(input-capture-x11-test EXCLUDE_FROM_ALL
    tests/input_capture_x11_test.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64-cli/LinuxNotification.cpp
)
target_include_directories(input-capture-x11-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/.. /usr/include/SDL2)
set_target_properties(input-capture-x11-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(input-capture-x11-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(input-capture-x11-test PRIVATE Common Project64-core Qt6::Widgets SDL2 X11 :libXtst.so.6)
add_dependencies(input-capture-x11-test Project64-input)
if(TARGET Project64-input-qtconfig)
    add_dependencies(input-capture-x11-test Project64-input-qtconfig)
endif()

# A virtual (uinput) gamepad: hot plug, assignment in the dialog, GetKeys, unplug, plug again.
add_executable(input-gamepad-test EXCLUDE_FROM_ALL
    tests/input_gamepad_test.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64-cli/LinuxNotification.cpp
)
target_include_directories(input-gamepad-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(input-gamepad-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(input-gamepad-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(input-gamepad-test PRIVATE Common Project64-core Qt6::Widgets)
add_dependencies(input-gamepad-test Project64-input)
if(TARGET Project64-input-qtconfig)
    add_dependencies(input-gamepad-test Project64-input-qtconfig)
endif()

# The audio plugin's output (SDL "disk" driver) at the volume of the settings; open/close cycles.
add_executable(audio-test EXCLUDE_FROM_ALL
    tests/audio_test.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64-cli/LinuxNotification.cpp
)
target_include_directories(audio-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(audio-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(audio-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(audio-test PRIVATE Common Project64-core Qt6::Widgets ${CMAKE_DL_LIBS})
add_dependencies(audio-test Project64-audio)

# The vector N64 controller widget of the input dialog (geometry, states, PNGs).
add_executable(controller-widget-test EXCLUDE_FROM_ALL
    tests/controller_widget_test.cpp
    ../Project64-input/N64ControllerWidget.cpp
)
set_target_properties(controller-widget-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_link_libraries(controller-widget-test PRIVATE Qt6::Widgets)

# Regression test for the video plugin's Qt configuration dialog.
add_executable(plugin-config-test EXCLUDE_FROM_ALL
    tests/plugin_config_test.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64-cli/LinuxNotification.cpp
)
target_include_directories(plugin-config-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(plugin-config-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(plugin-config-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(plugin-config-test PRIVATE Common Project64-core Qt6::Widgets)
add_dependencies(plugin-config-test Project64-video Project64-audio Project64-input)
if(TARGET Project64-video-qtconfig)
    add_dependencies(plugin-config-test Project64-video-qtconfig Project64-audio-qtconfig)
    if(TARGET Project64-input-qtconfig)
        add_dependencies(plugin-config-test Project64-input-qtconfig)
    endif()
endif()

# Regression test for the Qt ROM browser.
add_executable(rombrowser-test EXCLUDE_FROM_ALL
    tests/rombrowser_test.cpp
    RomBrowser.cpp
    RomInformation.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64-cli/LinuxNotification.cpp
)
target_include_directories(rombrowser-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(rombrowser-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON AUTOMOC ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(rombrowser-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(rombrowser-test PRIVATE Common Project64-core Qt6::Widgets Qt6::Test)

# Regression test for the Qt main menu (port of CMainMenu).
add_executable(mainmenu-test EXCLUDE_FROM_ALL
    tests/mainmenu_test.cpp
    MainWindow.cpp
    MainMenu.cpp
    ${VORTEX_SOURCES}
    VirtualKeys.cpp
    Settings/ShortCutsPage.cpp
    Dialogs/AboutDialog.cpp
    Dialogs/LogOptionsDialog.cpp
    ScreenSaverInhibit.cpp
    PresentSync.cpp
    Dialogs/FirstRun.cpp
    Dialogs/CheatDialog.cpp
    Dialogs/EnhancementDialog.cpp
    Dialogs/EnhancementTree.cpp
    about.qrc
    ../Project64/UserInterface/MenuShortCuts.cpp
    ../Project64/UserInterface/DiscordRPC.cpp
    QtNotification.cpp
    RomBrowser.cpp
    RomInformation.cpp
    Settings/SettingsDialog.cpp
    Settings/SettingsPage.cpp
    Settings/SettingsPages.cpp
    Settings/VortexPages.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64/Plugins/PluginList.cpp
    ../Project64-cli/LinuxNotification.cpp
    $<TARGET_OBJECTS:Project64-debugger-ui>
    $<TARGET_OBJECTS:Project64-debugger-backend>
    $<TARGET_OBJECTS:Project64-debugger-scripts>
)
target_include_directories(mainmenu-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(mainmenu-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(mainmenu-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(mainmenu-test PRIVATE Common Project64-core Qt6::Widgets Qt6::DBus Qt6::Test X11::X11 vendored_png discord_rpc)

# Lifecycle test with a real ROM (needs an X server, see Scripts/linux/run-gui-tests.sh)
add_executable(lifecycle-test EXCLUDE_FROM_ALL
    tests/lifecycle_test.cpp
    MainWindow.cpp
    MainMenu.cpp
    ${VORTEX_SOURCES}
    VirtualKeys.cpp
    Settings/ShortCutsPage.cpp
    Dialogs/AboutDialog.cpp
    Dialogs/LogOptionsDialog.cpp
    ScreenSaverInhibit.cpp
    PresentSync.cpp
    Dialogs/FirstRun.cpp
    Dialogs/CheatDialog.cpp
    Dialogs/EnhancementDialog.cpp
    Dialogs/EnhancementTree.cpp
    about.qrc
    ../Project64/UserInterface/MenuShortCuts.cpp
    ../Project64/UserInterface/DiscordRPC.cpp
    QtNotification.cpp
    RomBrowser.cpp
    RomInformation.cpp
    Settings/SettingsDialog.cpp
    Settings/SettingsPage.cpp
    Settings/SettingsPages.cpp
    Settings/VortexPages.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64/Plugins/PluginList.cpp
    $<TARGET_OBJECTS:Project64-debugger-ui>
    $<TARGET_OBJECTS:Project64-debugger-backend>
    $<TARGET_OBJECTS:Project64-debugger-scripts>
)
target_include_directories(lifecycle-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(lifecycle-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(lifecycle-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(lifecycle-test PRIVATE Common Project64-core Qt6::Widgets Qt6::DBus X11::X11 vendored_png discord_rpc)
add_dependencies(lifecycle-test Project64-video Project64-rsp Project64-audio Project64-input)

# The decisions of the synchronization of the presentation with the monitor (offscreen, no game)
add_executable(present-policy-test EXCLUDE_FROM_ALL
    tests/present_policy_test.cpp
    PresentSync.cpp
)
target_include_directories(present-policy-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(present-policy-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(present-policy-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(present-policy-test PRIVATE Common Project64-core Qt6::Widgets)

# The synchronization of the presentation with the monitor, with a real ROM and each video plugin (needs an X server)
add_executable(present-sync-test EXCLUDE_FROM_ALL
    tests/present_sync_test.cpp
    MainWindow.cpp
    MainMenu.cpp
    ${VORTEX_SOURCES}
    VirtualKeys.cpp
    Settings/ShortCutsPage.cpp
    Dialogs/AboutDialog.cpp
    Dialogs/LogOptionsDialog.cpp
    ScreenSaverInhibit.cpp
    PresentSync.cpp
    Dialogs/FirstRun.cpp
    Dialogs/CheatDialog.cpp
    Dialogs/EnhancementDialog.cpp
    Dialogs/EnhancementTree.cpp
    about.qrc
    ../Project64/UserInterface/MenuShortCuts.cpp
    ../Project64/UserInterface/DiscordRPC.cpp
    QtNotification.cpp
    RomBrowser.cpp
    RomInformation.cpp
    Settings/SettingsDialog.cpp
    Settings/SettingsPage.cpp
    Settings/SettingsPages.cpp
    Settings/VortexPages.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64/Plugins/PluginList.cpp
    $<TARGET_OBJECTS:Project64-debugger-ui>
    $<TARGET_OBJECTS:Project64-debugger-backend>
    $<TARGET_OBJECTS:Project64-debugger-scripts>
)
target_include_directories(present-sync-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(present-sync-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(present-sync-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(present-sync-test PRIVATE Common Project64-core Qt6::Widgets Qt6::DBus X11::X11 vendored_png discord_rpc)
add_dependencies(present-sync-test Project64-video Project64-rsp Project64-audio Project64-input)

# GLideN64 as the video plugin with a real ROM (needs an X server)
add_executable(gliden64-test EXCLUDE_FROM_ALL
    tests/gliden64_test.cpp
    MainWindow.cpp
    MainMenu.cpp
    ${VORTEX_SOURCES}
    VirtualKeys.cpp
    Settings/ShortCutsPage.cpp
    Dialogs/AboutDialog.cpp
    Dialogs/LogOptionsDialog.cpp
    ScreenSaverInhibit.cpp
    PresentSync.cpp
    Dialogs/FirstRun.cpp
    Dialogs/CheatDialog.cpp
    Dialogs/EnhancementDialog.cpp
    Dialogs/EnhancementTree.cpp
    about.qrc
    ../Project64/UserInterface/MenuShortCuts.cpp
    ../Project64/UserInterface/DiscordRPC.cpp
    QtNotification.cpp
    RomBrowser.cpp
    RomInformation.cpp
    Settings/SettingsDialog.cpp
    Settings/SettingsPage.cpp
    Settings/SettingsPages.cpp
    Settings/VortexPages.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64/Plugins/PluginList.cpp
    $<TARGET_OBJECTS:Project64-debugger-ui>
    $<TARGET_OBJECTS:Project64-debugger-backend>
    $<TARGET_OBJECTS:Project64-debugger-scripts>
)
target_include_directories(gliden64-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(gliden64-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(gliden64-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(gliden64-test PRIVATE Common Project64-core Qt6::Widgets Qt6::DBus X11::X11 vendored_png discord_rpc)
add_dependencies(gliden64-test Project64-video Project64-rsp Project64-audio Project64-input)
if(TARGET GLideN64)
    add_dependencies(gliden64-test GLideN64)
endif()

# Differential test of the x64 recompiler against the interpreter (needs an X server)
add_executable(jit-fuzz-test EXCLUDE_FROM_ALL
    tests/jit_fuzz_test.cpp
    MainWindow.cpp
    MainMenu.cpp
    ${VORTEX_SOURCES}
    VirtualKeys.cpp
    Settings/ShortCutsPage.cpp
    Dialogs/AboutDialog.cpp
    Dialogs/LogOptionsDialog.cpp
    ScreenSaverInhibit.cpp
    PresentSync.cpp
    Dialogs/FirstRun.cpp
    Dialogs/CheatDialog.cpp
    Dialogs/EnhancementDialog.cpp
    Dialogs/EnhancementTree.cpp
    about.qrc
    ../Project64/UserInterface/MenuShortCuts.cpp
    ../Project64/UserInterface/DiscordRPC.cpp
    QtNotification.cpp
    RomBrowser.cpp
    RomInformation.cpp
    Settings/SettingsDialog.cpp
    Settings/SettingsPage.cpp
    Settings/SettingsPages.cpp
    Settings/VortexPages.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64/Plugins/PluginList.cpp
    $<TARGET_OBJECTS:Project64-debugger-ui>
    $<TARGET_OBJECTS:Project64-debugger-backend>
    $<TARGET_OBJECTS:Project64-debugger-scripts>
)
target_include_directories(jit-fuzz-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(jit-fuzz-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(jit-fuzz-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(jit-fuzz-test PRIVATE Common Project64-core Qt6::Widgets Qt6::DBus X11::X11 vendored_png discord_rpc)
add_dependencies(jit-fuzz-test Project64-video Project64-rsp Project64-audio Project64-input)

# Debugger windows against a real ROM (needs an X server)
add_executable(debugger-test EXCLUDE_FROM_ALL
    tests/debugger_test.cpp
    MainWindow.cpp
    MainMenu.cpp
    ${VORTEX_SOURCES}
    VirtualKeys.cpp
    Settings/ShortCutsPage.cpp
    Dialogs/AboutDialog.cpp
    Dialogs/LogOptionsDialog.cpp
    ScreenSaverInhibit.cpp
    PresentSync.cpp
    Dialogs/FirstRun.cpp
    Dialogs/CheatDialog.cpp
    Dialogs/EnhancementDialog.cpp
    Dialogs/EnhancementTree.cpp
    about.qrc
    ../Project64/UserInterface/MenuShortCuts.cpp
    ../Project64/UserInterface/DiscordRPC.cpp
    QtNotification.cpp
    RomBrowser.cpp
    RomInformation.cpp
    Settings/SettingsDialog.cpp
    Settings/SettingsPage.cpp
    Settings/SettingsPages.cpp
    Settings/VortexPages.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64/Plugins/PluginList.cpp
    $<TARGET_OBJECTS:Project64-debugger-ui>
    $<TARGET_OBJECTS:Project64-debugger-backend>
    $<TARGET_OBJECTS:Project64-debugger-scripts>
)
target_include_directories(debugger-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(debugger-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(debugger-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(debugger-test PRIVATE Common Project64-core Qt6::Widgets Qt6::DBus X11::X11 vendored_png discord_rpc)
add_dependencies(debugger-test Project64-video Project64-rsp Project64-audio Project64-input)

# Scripting (Scripts window and JavaScript engine) against a real ROM (needs an X server)
add_executable(script-test EXCLUDE_FROM_ALL
    tests/script_test.cpp
    MainWindow.cpp
    MainMenu.cpp
    ${VORTEX_SOURCES}
    VirtualKeys.cpp
    Settings/ShortCutsPage.cpp
    Dialogs/AboutDialog.cpp
    Dialogs/LogOptionsDialog.cpp
    ScreenSaverInhibit.cpp
    PresentSync.cpp
    Dialogs/FirstRun.cpp
    Dialogs/CheatDialog.cpp
    Dialogs/EnhancementDialog.cpp
    Dialogs/EnhancementTree.cpp
    about.qrc
    ../Project64/UserInterface/MenuShortCuts.cpp
    ../Project64/UserInterface/DiscordRPC.cpp
    QtNotification.cpp
    RomBrowser.cpp
    RomInformation.cpp
    Settings/SettingsDialog.cpp
    Settings/SettingsPage.cpp
    Settings/SettingsPages.cpp
    Settings/VortexPages.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64/Plugins/PluginList.cpp
    $<TARGET_OBJECTS:Project64-debugger-ui>
    $<TARGET_OBJECTS:Project64-debugger-backend>
    $<TARGET_OBJECTS:Project64-debugger-scripts>
)
target_include_directories(script-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(script-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(script-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(script-test PRIVATE Common Project64-core Qt6::Widgets Qt6::DBus X11::X11 vendored_png discord_rpc)
add_dependencies(script-test Project64-video Project64-rsp Project64-audio Project64-input)

# Main window behaviour (status bar, place and size, focus, drop) against a real ROM (needs an X server)
add_executable(mainwindow-test EXCLUDE_FROM_ALL
    tests/mainwindow_test.cpp
    MainWindow.cpp
    MainMenu.cpp
    ${VORTEX_SOURCES}
    VirtualKeys.cpp
    Settings/ShortCutsPage.cpp
    Dialogs/AboutDialog.cpp
    Dialogs/LogOptionsDialog.cpp
    ScreenSaverInhibit.cpp
    PresentSync.cpp
    Dialogs/FirstRun.cpp
    Dialogs/CheatDialog.cpp
    Dialogs/EnhancementDialog.cpp
    Dialogs/EnhancementTree.cpp
    about.qrc
    ../Project64/UserInterface/MenuShortCuts.cpp
    ../Project64/UserInterface/DiscordRPC.cpp
    QtNotification.cpp
    RomBrowser.cpp
    RomInformation.cpp
    Settings/SettingsDialog.cpp
    Settings/SettingsPage.cpp
    Settings/SettingsPages.cpp
    Settings/VortexPages.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64/Plugins/PluginList.cpp
    $<TARGET_OBJECTS:Project64-debugger-ui>
    $<TARGET_OBJECTS:Project64-debugger-backend>
    $<TARGET_OBJECTS:Project64-debugger-scripts>
)
target_include_directories(mainwindow-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(mainwindow-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(mainwindow-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(mainwindow-test PRIVATE Common Project64-core Qt6::Widgets Qt6::DBus X11::X11 vendored_png discord_rpc)
add_dependencies(mainwindow-test Project64-video Project64-rsp Project64-audio Project64-input)

# Navigation of the Vortex64 frontend: library, search, favourites, history, views, settings (offscreen Qt)
add_executable(vortex-test EXCLUDE_FROM_ALL
    tests/vortex_test.cpp
    MainWindow.cpp
    MainMenu.cpp
    ${VORTEX_SOURCES}
    VirtualKeys.cpp
    Settings/ShortCutsPage.cpp
    Dialogs/AboutDialog.cpp
    Dialogs/LogOptionsDialog.cpp
    ScreenSaverInhibit.cpp
    PresentSync.cpp
    Dialogs/FirstRun.cpp
    Dialogs/CheatDialog.cpp
    Dialogs/EnhancementDialog.cpp
    Dialogs/EnhancementTree.cpp
    about.qrc
    ../Project64/UserInterface/MenuShortCuts.cpp
    ../Project64/UserInterface/DiscordRPC.cpp
    QtNotification.cpp
    RomBrowser.cpp
    RomInformation.cpp
    Settings/SettingsDialog.cpp
    Settings/SettingsPage.cpp
    Settings/SettingsPages.cpp
    Settings/VortexPages.cpp
    ../Project64/Settings/UISettings.cpp
    ../Project64/Plugins/PluginList.cpp
    $<TARGET_OBJECTS:Project64-debugger-ui>
    $<TARGET_OBJECTS:Project64-debugger-backend>
    $<TARGET_OBJECTS:Project64-debugger-scripts>
)
target_include_directories(vortex-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(vortex-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(vortex-test PRIVATE QT_NO_KEYWORDS)
target_link_libraries(vortex-test PRIVATE Common Project64-core Qt6::Widgets Qt6::DBus X11::X11 vendored_png discord_rpc)
add_dependencies(vortex-test Project64-video Project64-rsp Project64-audio Project64-input)

# The check of the plugin directory (Qt only)
add_executable(plugindir-test EXCLUDE_FROM_ALL tests/plugindir_test.cpp Vortex/PluginDirCheck.cpp)
target_include_directories(plugindir-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(plugindir-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_link_libraries(plugindir-test PRIVATE Qt6::Widgets)

# The same application as vortex-test, for the tests of the interface: the languages and the layout (offscreen Qt)
get_target_property(VORTEX_APP_TEST_SOURCES vortex-test SOURCES)
list(REMOVE_ITEM VORTEX_APP_TEST_SOURCES tests/vortex_test.cpp)
foreach(UI_TEST language layout input-ingame hotkey-pause first-run)
    string(REPLACE "-" "_" UI_TEST_FILE ${UI_TEST})
    add_executable(${UI_TEST}-test EXCLUDE_FROM_ALL tests/${UI_TEST_FILE}_test.cpp ${VORTEX_APP_TEST_SOURCES})
    target_include_directories(${UI_TEST}-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/.. /usr/include/SDL2)
    set_target_properties(${UI_TEST}-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    target_compile_definitions(${UI_TEST}-test PRIVATE QT_NO_KEYWORDS)
    target_link_libraries(${UI_TEST}-test PRIVATE Common Project64-core Qt6::Widgets Qt6::DBus X11::X11 vendored_png discord_rpc SDL2 :libXtst.so.6)
    add_dependencies(${UI_TEST}-test Project64-video Project64-rsp Project64-audio Project64-input Project64-input-qtconfig)
endforeach()

# Plugin loading test (dlopen/dlsym, validation, load errors, cycles)
add_executable(plugins-test EXCLUDE_FROM_ALL
    tests/plugins_test.cpp
    ../Project64-cli/LinuxNotification.cpp
)
target_include_directories(plugins-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(plugins-test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_link_libraries(plugins-test PRIVATE Common Project64-core)
add_dependencies(plugins-test Project64-video Project64-rsp Project64-audio Project64-input)

# Debugger back end of upstream (Project64/UserInterface/Debugger), compiled unchanged: the
# breakpoints, symbol table, debug MMU access, memory scanner, assembler and the CPU/DMA logs
add_library(Project64-debugger-backend OBJECT
    ../Project64/UserInterface/Debugger/Breakpoints.cpp
    ../Project64/UserInterface/Debugger/Symbols.cpp
    ../Project64/UserInterface/Debugger/DebugMMU.cpp
    ../Project64/UserInterface/Debugger/MemoryScanner.cpp
    ../Project64/UserInterface/Debugger/Assembler.cpp
    ../Project64/UserInterface/Debugger/CPULog.cpp
    ../Project64/UserInterface/Debugger/DMALog.cpp
)
target_include_directories(Project64-debugger-backend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../Project64/UserInterface/Debugger)
set_target_properties(Project64-debugger-backend PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(Project64-debugger-backend PRIVATE Common Project64-core Qt6::Widgets)
target_compile_definitions(Project64-debugger-backend PRIVATE QT_NO_KEYWORDS)

# The Qt debugger windows and CDebuggerUI (shared by the GUI and the tests)
add_library(Project64-debugger-ui OBJECT
    Debugger/DebugCommon.cpp
    Debugger/DebugWindow.cpp
    Debugger/DebuggerShell.cpp
    Debugger/DebugDialogs.cpp
    Debugger/DebuggerUI.cpp
    Debugger/RegisterTabs.cpp
    Debugger/CommandsView.cpp
    Debugger/SymbolsView.cpp
    Debugger/StackWindows.cpp
    Debugger/TlbView.cpp
    Debugger/ExceptionBreakpointsView.cpp
    Debugger/DmaLogView.cpp
    Debugger/CpuLogView.cpp
    Debugger/MemoryDumpView.cpp
    Debugger/HexEditWidget.cpp
    Debugger/MemoryView.cpp
    Debugger/MemorySearchView.cpp
    Debugger/ScriptsView.cpp
)
target_include_directories(Project64-debugger-ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_target_properties(Project64-debugger-ui PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(Project64-debugger-ui PRIVATE QT_NO_KEYWORDS)
target_link_libraries(Project64-debugger-ui PRIVATE Common Project64-core Qt6::Widgets)

# JavaScript script engine of the debugger (duktape): the upstream ScriptSystem/ScriptAPI
# sources with the Win32 parts behind #ifdef _WIN32 and ScriptShim/ providing the names of
# <windows.h> that they use (threads, events, Winsock on BSD sockets)
# libpng 1.4.7 of the tree (N64Image PNG import/export)
file(GLOB VENDORED_PNG_SOURCES ../3rdParty/png/png*.c)
list(FILTER VENDORED_PNG_SOURCES EXCLUDE REGEX "pngtest\\.c$")
add_library(vendored_png STATIC ${VENDORED_PNG_SOURCES})
target_include_directories(vendored_png PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../3rdParty)
target_compile_definitions(vendored_png PRIVATE PNG_SKIP_SETJMP_CHECK)
target_link_libraries(vendored_png PUBLIC vendored_zlib)
set_target_properties(vendored_png PROPERTIES POSITION_INDEPENDENT_CODE ON)
# hidden: an exported png_* of libpng 1.4.7 would replace the libpng 1.6 that Qt loads
target_compile_options(vendored_png PRIVATE -w -fvisibility=hidden)

set(DEBUGGER_SCRIPT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Project64/UserInterface/Debugger)
file(GLOB DEBUGGER_SCRIPTAPI_SOURCES ${DEBUGGER_SCRIPT_DIR}/ScriptAPI/*.cpp)
add_library(Project64-debugger-scripts OBJECT
    ${DEBUGGER_SCRIPT_DIR}/ScriptSystem.cpp
    ${DEBUGGER_SCRIPT_DIR}/ScriptInstance.cpp
    ${DEBUGGER_SCRIPT_DIR}/ScriptWorker.cpp
    ${DEBUGGER_SCRIPTAPI_SOURCES}
    ../3rdParty/duktape/duktape.cpp
    ../3rdParty/duktape/duk_module_duktape.cpp
    Debugger/ScriptShim/ScriptShim.cpp
    Debugger/ScriptGlue.cpp
)
target_include_directories(Project64-debugger-scripts BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Debugger/ScriptShim)
target_include_directories(Project64-debugger-scripts PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/../3rdParty ${DEBUGGER_SCRIPT_DIR} ${DEBUGGER_SCRIPT_DIR}/ScriptAPI)
set_target_properties(Project64-debugger-scripts PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(Project64-debugger-scripts PRIVATE Common Project64-core Qt6::Widgets vendored_png)
target_compile_definitions(Project64-debugger-scripts PRIVATE PNG_SKIP_SETJMP_CHECK)
# MSVC accepts `return duk_throw(ctx)` in functions returning pointers; GCC needs -fpermissive for that
set_source_files_properties(${DEBUGGER_SCRIPT_DIR}/ScriptAPI/JSSocketWorker.cpp ${DEBUGGER_SCRIPT_DIR}/ScriptAPI/ScriptAPI_Socket.cpp ${DEBUGGER_SCRIPT_DIR}/ScriptAPI/ScriptAPI_N64Image.cpp PROPERTIES COMPILE_OPTIONS -fpermissive)
target_compile_definitions(Project64-debugger-scripts PRIVATE QT_NO_KEYWORDS)

# The icons of Vortex64 (packaging/linux/icons): the drawing of the application, written as SVG and PNG
add_executable(vortex64-icongen EXCLUDE_FROM_ALL Vortex/tools/icongen.cpp)
target_include_directories(vortex64-icongen PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(vortex64-icongen PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
target_compile_definitions(vortex64-icongen PRIVATE QT_NO_KEYWORDS)
target_link_libraries(vortex64-icongen PRIVATE Qt6::Gui)
