cmake_minimum_required(VERSION 3.16)
project(Vortex64-core CXX)

# On Windows, UpdateVersion.vcxproj runs before the build and regenerates
# Version.h from Version.h.in, filling in git info. Version.h.in is already
# valid, self-contained C (GIT_REVISION etc. as empty/"Unknown" literals), so
# a straight copy is a correct build without needing to reimplement that
# tool; it is written to the build tree, not back into the source tree.
set(P64_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/Vortex64-core)
file(MAKE_DIRECTORY ${P64_GENERATED_DIR})
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Version.h.in ${P64_GENERATED_DIR}/Version.h COPYONLY)

# Source list mirrors Vortex64-core.vcxproj's <ClCompile> entries exactly
# (case-sensitive — this matters on Linux where the Windows build never
# noticed a mismatch). N64System/Recompiler/{Arm,Aarch64,x86,x64-86}/*.cpp
# and Plugins/*.cpp all self-select their contents behind arch #if guards,
# so all of them are compiled unconditionally, matching the .vcxproj.
add_library(Vortex64-core STATIC
    3rdParty/7zip.cpp
    AppInit.cpp
    Logging.cpp
    Multilanguage/Language.cpp
    N64System/EmulationThread.cpp
    N64System/Enhancement/Enhancement.cpp
    N64System/Enhancement/EnhancementFile.cpp
    N64System/Enhancement/EnhancementList.cpp
    N64System/Enhancement/Enhancements.cpp
    N64System/FramePerSecond.cpp
    N64System/Interpreter/InterpreterOps.cpp
    N64System/MemoryHandler/AudioInterfaceHandler.cpp
    N64System/MemoryHandler/CartridgeDomain1Address1Handler.cpp
    N64System/MemoryHandler/CartridgeDomain1Address3Handler.cpp
    N64System/MemoryHandler/CartridgeDomain2Address1Handler.cpp
    N64System/MemoryHandler/CartridgeDomain2Address2Handler.cpp
    N64System/MemoryHandler/DisplayControlRegHandler.cpp
    N64System/MemoryHandler/ISViewerHandler.cpp
    N64System/MemoryHandler/MIPSInterfaceHandler.cpp
    N64System/MemoryHandler/PeripheralInterfaceHandler.cpp
    N64System/MemoryHandler/PifRamHandler.cpp
    N64System/MemoryHandler/RDRAMInterfaceHandler.cpp
    N64System/MemoryHandler/RDRAMRegistersHandler.cpp
    N64System/MemoryHandler/RomMemoryHandler.cpp
    N64System/MemoryHandler/SerialInterfaceHandler.cpp
    N64System/MemoryHandler/SPRegistersHandler.cpp
    N64System/MemoryHandler/VideoInterfaceHandler.cpp
    N64System/Mips/Disk.cpp
    N64System/Mips/GBCart.cpp
    N64System/Mips/MemoryVirtualMem.cpp
    N64System/Mips/Mempak.cpp
    N64System/Mips/R4300iInstruction.cpp
    N64System/Mips/Register.cpp
    N64System/Mips/Rumblepak.cpp
    N64System/Mips/SystemEvents.cpp
    N64System/Mips/SystemTiming.cpp
    N64System/Mips/TLB.cpp
    N64System/Mips/Transferpak.cpp
    N64System/N64Disk.cpp
    N64System/N64Rom.cpp
    N64System/N64System.cpp
    N64System/Profiling.cpp
    N64System/Recompiler/Aarch64/Aarch64ops.cpp
    N64System/Recompiler/Aarch64/Aarch64RecompilerOps.cpp
    N64System/Recompiler/Aarch64/Aarch64RegInfo.cpp
    N64System/Recompiler/Arm/ArmOps.cpp
    N64System/Recompiler/Arm/ArmRecompilerOps.cpp
    N64System/Recompiler/Arm/ArmRegInfo.cpp
    N64System/Recompiler/CodeBlock.cpp
    N64System/Recompiler/CodeSection.cpp
    N64System/Recompiler/ExitInfo.cpp
    N64System/Recompiler/FunctionInfo.cpp
    N64System/Recompiler/FunctionMap.cpp
    N64System/Recompiler/JumpInfo.cpp
    N64System/Recompiler/LoopAnalysis.cpp
    N64System/Recompiler/Recompiler.cpp
    N64System/Recompiler/RecompilerMemory.cpp
    N64System/Recompiler/RecompilerOps.cpp
    N64System/Recompiler/RegBase.cpp
    N64System/Recompiler/x64-86/x64ops.cpp
    N64System/Recompiler/x64-86/x64RecompilerOps.cpp
    N64System/Recompiler/x64-86/x64RegInfo.cpp
    N64System/Recompiler/x86/x86ops.cpp
    N64System/Recompiler/x86/x86RecompilerOps.cpp
    N64System/Recompiler/x86/x86RegInfo.cpp
    N64System/SaveType/Eeprom.cpp
    N64System/SaveType/FlashRam.cpp
    N64System/SaveType/Sram.cpp
    N64System/SpeedLimiter.cpp
    N64System/SystemGlobals.cpp
    Plugins/AudioPlugin.cpp
    Plugins/ControllerPlugin.cpp
    Plugins/GFXPlugin.cpp
    Plugins/PluginBase.cpp
    Plugins/Plugin.cpp
    Plugins/RSPPlugin.cpp
    RomList/RomList.cpp
    Settings.cpp
    Settings/DebugSettings.cpp
    Settings/GameSettings.cpp
    Settings/LoggingSettings.cpp
    Settings/SettingType/SettingsType-Application.cpp
    Settings/SettingType/SettingsType-ApplicationIndex.cpp
    Settings/SettingType/SettingsType-ApplicationPath.cpp
    Settings/SettingType/SettingsType-GameSetting.cpp
    Settings/SettingType/SettingsType-GameSettingIndex.cpp
    Settings/SettingType/SettingsType-RDB.cpp
    Settings/SettingType/SettingsType-RDBCpuType.cpp
    Settings/SettingType/SettingsType-RDBLinking.cpp
    Settings/SettingType/SettingsType-RDBOnOff.cpp
    Settings/SettingType/SettingsType-RDBRamSize.cpp
    Settings/SettingType/SettingsType-RDBSaveChip.cpp
    Settings/SettingType/SettingsType-RDBUser.cpp
    Settings/SettingType/SettingsType-RelativePath.cpp
    Settings/SettingType/SettingsType-RomDatabase.cpp
    Settings/SettingType/SettingsType-RomDatabaseIndex.cpp
    Settings/SettingType/SettingsType-RomDatabaseSetting.cpp
    Settings/SettingType/SettingsType-SelectedDirectory.cpp
    Settings/SettingType/SettingsType-TempBool.cpp
    Settings/SettingType/SettingsType-TempNumber.cpp
    Settings/SettingType/SettingsType-TempString.cpp
    Settings/SystemSettings.cpp
    stdafx.cpp
)

target_include_directories(Vortex64-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    # <Vortex64-core/Version.h> for the frontends (About dialog)
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_include_directories(Vortex64-core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdParty/softfloat-3e/source/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdParty/asmjit/src
    # On Windows, PropertySheets/Platform.props puts Source/3rdParty on every
    # project's include path solution-wide (how e.g. `#include <7zip/7z.h>`
    # resolves in 3rdParty/7zip.h); add it explicitly here too.
    ${CMAKE_CURRENT_SOURCE_DIR}/../3rdParty
    ${P64_GENERATED_DIR}
)
set_target_properties(Vortex64-core PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(Vortex64-core PUBLIC
    Common
    Vortex64-rsp-core
    asmjit
    softfloat
    sevenzip_decoder
    minizip
    vendored_zlib
)
