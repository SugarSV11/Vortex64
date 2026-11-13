cmake_minimum_required(VERSION 3.16)
project(Vortex64-rsp CXX)

# ---------------------------------------------------------------------------
# Linux-native build of the RSP plugin.
#
# Upstream only ships this as a Visual Studio project. The RSP command/
# register debugger windows ("RSP Command.cpp", "Debugger/RSPRegistersUI.cpp")
# are Win32 dialog UIs that have not been ported; on non-Windows builds they
# are excluded and their externally-visible functions are instead provided
# as no-op stand-ins directly inside those two source files (guarded by
# #ifdef _WIN32 / #else), so the debugger window is effectively disabled but
# the rest of the plugin still links and runs.
#
# "Interpreter CPU.cpp" is dead/orphaned code (not referenced by the
# Windows .vcxproj either) and is intentionally excluded.
# ---------------------------------------------------------------------------

add_library(Vortex64-rsp SHARED
    breakpoint.cpp
    Debugger/RSPDebuggerUI.cpp
    Debugger/RSPRegistersUI.cpp
    Main.cpp
    "RSP Command.cpp"
)

target_include_directories(Vortex64-rsp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(Vortex64-rsp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(Vortex64-rsp PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

target_link_libraries(Vortex64-rsp PRIVATE
    Common
    Settings
    Vortex64-rsp-core
)

if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(Vortex64-rsp PRIVATE Threads::Threads)
endif()
