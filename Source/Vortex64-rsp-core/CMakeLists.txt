cmake_minimum_required(VERSION 3.16)
project(Vortex64-rsp-core CXX)

set(P64RSPCORE_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/Vortex64-rsp-core)
file(MAKE_DIRECTORY ${P64RSPCORE_GENERATED_DIR})
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Version.h.in ${P64RSPCORE_GENERATED_DIR}/Version.h COPYONLY)

add_library(Vortex64-rsp-core STATIC
    cpu/RspClamp.cpp
    cpu/RSPCpu.cpp
    cpu/RSPInstruction-x64.cpp
    cpu/RSPInstruction-x86.cpp
    cpu/RSPInterpreterOps.cpp
    cpu/RspLog.cpp
    cpu/RspMemory.cpp
    cpu/RSPRegister.cpp
    cpu/RSPRegisterHandler.cpp
    cpu/RSPRegisterHandlerPlugin.cpp
    cpu/RspSystem.cpp
    cpu/RspTypes.cpp
    Hle/alist_audio.cpp
    Hle/alist.cpp
    Hle/alist_naudio.cpp
    Hle/alist_nead.cpp
    Hle/audio.cpp
    Hle/cicx105.cpp
    Hle/hle.cpp
    Hle/HleTask.cpp
    Hle/jpeg.cpp
    Hle/mem.cpp
    Hle/mp3.cpp
    Hle/musyx.cpp
    Recompiler/Mmx.cpp
    Recompiler/RspAssembler.cpp
    Recompiler/RspCodeBlock.cpp
    Recompiler/RspProfiling.cpp
    Recompiler/RspRecompilerAnalysis.cpp
    Recompiler/RspRecompilerCPU-x64.cpp
    Recompiler/RspRecompilerCPU-x86.cpp
    Recompiler/RspRecompilerOps-x64.cpp
    Recompiler/RspRecompilerOps-x86.cpp
    Recompiler/RspRecompilerSections.cpp
    Recompiler/RspRegState.cpp
    Recompiler/Sse.cpp
    Recompiler/X86.cpp
    RSPDebugger.cpp
    RSPInfo.cpp
    Settings/RspSettings.cpp
)

target_include_directories(Vortex64-rsp-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# On Windows, PropertySheets/Platform.props puts Source/3rdParty on every
# project's include path solution-wide, which is how e.g. `#include
# <zlib/zlib.h>` resolves; this CMake tree doesn't have an equivalent global
# property sheet, so add it explicitly here.
target_include_directories(Vortex64-rsp-core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../3rdParty)
target_include_directories(Vortex64-rsp-core PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)
set_target_properties(Vortex64-rsp-core PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(Vortex64-rsp-core PUBLIC Common asmjit vendored_zlib)
