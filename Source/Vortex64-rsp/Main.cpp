#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Settings/Settings.h"
#include <Common/StdString.h>
#include <stdint.h>

#include "Debugger/RSPDebuggerUI.h"
#include "Debugger/RSPRegistersUI.h"
#include "RSP Command.h"
#include "Rsp.h"
#include "breakpoint.h"
#include "resource.h"
#include <Vortex64-rsp-core/RSPInfo.h>
#include <Vortex64-rsp-core/Recompiler/RspProfiling.h>
#include <Vortex64-rsp-core/Recompiler/RspRecompilerCPU-x86.h>
#include <Vortex64-rsp-core/Settings/RspSettings.h>
#include <Vortex64-rsp-core/Settings/RspSettingsID.h>
#include <Vortex64-rsp-core/Version.h>
#include <Vortex64-rsp-core/cpu/RSPCpu.h>
#include <Vortex64-rsp-core/cpu/RSPRegisters.h>
#include <Vortex64-rsp-core/cpu/RspLog.h>
#include <Vortex64-rsp-core/cpu/RspMemory.h>
#include <Vortex64-rsp-core/cpu/RspSystem.h>
#include <Vortex64-rsp-core/cpu/RspTypes.h>

void ProcessMenuItem(int32_t ID);

DEBUG_INFO DebugInfo;
void * hinstDLL;
std::unique_ptr<RSPDebuggerUI> g_RSPDebuggerUI;

extern uint8_t * pLastSecondary;

// DLL info
const char * AppName(void)
{
    static stdstr_f Name("RSP %s", VER_FILE_VERSION_STR);
    return Name.c_str();
}
const char * AboutMsg(void)
{
    static stdstr_f Msg("RSP emulation plugin\nMade for Project64 (c)\nVersion %s\n\nby Jabo and Zilmar", VER_FILE_VERSION_STR);
    return Msg.c_str();
}

// Functions

uint32_t AsciiToHex(char * HexValue)
{
    size_t Finish, Count;
    uint32_t Value = 0;

    Finish = strlen(HexValue);
    if (Finish > 8)
    {
        Finish = 8;
    }

    for (Count = 0; Count < Finish; Count++)
    {
        Value = (Value << 4);
        switch (HexValue[Count])
        {
        case '0': break;
        case '1': Value += 1; break;
        case '2': Value += 2; break;
        case '3': Value += 3; break;
        case '4': Value += 4; break;
        case '5': Value += 5; break;
        case '6': Value += 6; break;
        case '7': Value += 7; break;
        case '8': Value += 8; break;
        case '9': Value += 9; break;
        case 'A': Value += 10; break;
        case 'a': Value += 10; break;
        case 'B': Value += 11; break;
        case 'b': Value += 11; break;
        case 'C': Value += 12; break;
        case 'c': Value += 12; break;
        case 'D': Value += 13; break;
        case 'd': Value += 13; break;
        case 'E': Value += 14; break;
        case 'e': Value += 14; break;
        case 'F': Value += 15; break;
        case 'f': Value += 15; break;
        default:
            Value = (Value >> 4);
            Count = Finish;
        }
    }
    return Value;
}

void DisplayError(char * Message, ...)
{
    char Msg[400];
    va_list ap;

    va_start(ap, Message);
    vsprintf(Msg, Message, ap);
    va_end(ap);
    fputs(&Msg[0], stderr);
}

/*
Function: CloseDLL
Purpose: This function is called when the emulator is closing
down allowing the DLL to de-initialize.
Input: None
Output: None
*/

EXPORT void CloseDLL(void)
{
    FreeRSP();
}

/*
Function: DllAbout
Purpose: This function is optional function that is provided
to give further information about the DLL.
Input: A handle to the window that calls this function
Output: None
*/

EXPORT void DllAbout(void * hParent)
{
    puts(AboutMsg());
}


/*
Function: GetDllInfo
Purpose: This function allows the emulator to gather information
about the DLL by filling in the PluginInfo structure.
Input: A pointer to a PLUGIN_INFO structure that needs to be
filled by the function. (see def above)
Output: None
*/

EXPORT void GetDllInfo(PLUGIN_INFO * PluginInfo)
{
    PluginInfo->Version = 0x0103;
    PluginInfo->Type = PLUGIN_TYPE_RSP;
#ifdef _DEBUG
    sprintf(PluginInfo->Name, "RSP debug plugin %s", VER_FILE_VERSION_STR);
#else
    sprintf(PluginInfo->Name, "RSP plugin %s", VER_FILE_VERSION_STR);
#endif
    PluginInfo->Reserved2 = false;
    PluginInfo->Reserved1 = true;
}

/*
Function: GetRspDebugInfo
Purpose: This function allows the emulator to gather information
about the debug capabilities of the DLL by filling in
the DebugInfo structure.
Input: A pointer to a RSPDEBUG_INFO structure that needs to be
filled by the function. (see def above)
Output: None
*/

EXPORT void GetRspDebugInfo(RSPDEBUG_INFO * _DebugInfo)
{
    // The RSP command/register debugger windows and breakpoint panel are
    // Win32 dialogs and have not been ported; report no debug capabilities
    // on other platforms so the core doesn't try to call into them.
    _DebugInfo->hRSPMenu = NULL;
    _DebugInfo->ProcessMenuItem = NULL;
    _DebugInfo->UseBPoints = false;
    _DebugInfo->BPPanelName[0] = '\0';
    _DebugInfo->Add_BPoint = NULL;
    _DebugInfo->CreateBPPanel = NULL;
    _DebugInfo->HideBPPanel = NULL;
    _DebugInfo->PaintBPPanel = NULL;
    _DebugInfo->RefreshBpoints = NULL;
    _DebugInfo->RemoveBpoint = NULL;
    _DebugInfo->ShowBPPanel = NULL;
    _DebugInfo->Enter_RSP_Commands_Window = NULL;
    _DebugInfo->RemoveAllBpoint = RemoveAllBpoint;
}

/*
Function: InitiateRSP
Purpose: This function is called when the DLL is started to give
information from the emulator that the N64 RSP interface needs.
Input: Rsp_Info is passed to this function which is defined
above.
CycleCount is the number of cycles between switching
control between the RSP and r4300i core.
Output: None
*/
EXPORT void InitiateRSP(RSP_INFO Rsp_Info, uint32_t * CycleCount)
{
    g_RSPDebuggerUI.reset(new RSPDebuggerUI(RSPSystem));
    g_RSPDebugger = g_RSPDebuggerUI.get();
    InitilizeRSP(Rsp_Info);
    *CycleCount = 0;
}

/*
Function: InitiateRSPDebugger
Purpose: This function is called when the DLL is started to give
information from the emulator that the N64 RSP
interface needs to integrate the debugger with the
rest of the emulator.
Input: DebugInfo is passed to this function which is defined
above.
Output: None
*/

EXPORT void InitiateRSPDebugger(DEBUG_INFO Debug_Info)
{
    DebugInfo = Debug_Info;
}


/*
Function: RomOpen
Purpose: This function is called when a ROM is opened.
Input: None
Output: None
*/

EXPORT void RomOpen(void)
{
    RspRomOpened();
    if (DebuggingEnabled)
    {
        EnableDebugging(true);
    }
}

/*
Function: RomClosed
Purpose: This function is called when a ROM is closed.
Input: None
Output: None
*/

EXPORT void RomClosed(void)
{
    RspRomClosed();
}


/*EXPORT void DllConfig(HWND hWnd)
{
	// DialogBox(hinstDLL, "RSPCONFIG", hWnd, ConfigDlgProc);
	DialogBox(hinstDLL, "RSPCONFIG", GetForegroundWindow(), ConfigDlgProc);
}*/

EXPORT void EnableDebugging(int Enabled)
{
    CRSPSettings::EnableDebugging(Enabled != 0);
    DebuggingEnabled = Enabled != 0;
    if (DebuggingEnabled)
    {
        BreakOnStart = GetSetting(Set_BreakOnStart) != 0;
        LogRDP = GetSetting(Set_LogRDP) != 0;
        LogAsmCode = GetSetting(Set_LogAsmCode) != 0;
        Profiling = GetSetting(Set_Profiling) != 0;
        IndvidualBlock = GetSetting(Set_IndvidualBlock) != 0;
        ShowErrors = GetSetting(Set_ShowErrors) != 0;
        SyncCPU = GetSetting(Set_SyncCPU) != 0;

#if defined(__i386__) || defined(_M_IX86)
        Compiler.bDest = GetSetting(Set_CheckDest) != 0;
        Compiler.bAccum = GetSetting(Set_Accum) != 0;
        Compiler.mmx = GetSetting(Set_Mmx) != 0;
        Compiler.mmx2 = GetSetting(Set_Mmx2) != 0;
        Compiler.sse = GetSetting(Set_Sse) != 0;
        Compiler.bSections = GetSetting(Set_Sections) != 0;
        Compiler.bReOrdering = GetSetting(Set_ReOrdering) != 0;
        Compiler.bGPRConstants = GetSetting(Set_GPRConstants) != 0;
        Compiler.bFlags = GetSetting(Set_Flags) != 0;
        Compiler.bAlignVector = GetSetting(Set_AlignVector) != 0;
#endif
    }
    fputs("FixMenuState()\n", stderr);
    if (LogRDP)
    {
        RDPLog.StartLog();
    }
    if (LogAsmCode)
    {
        StartCPULog();
    }
}

EXPORT void PluginLoaded(void)
{
    RspPluginLoaded();
}

void UseUnregisteredSetting(int /*SettingID*/)
{
    g_Notify->BreakPoint(__FILE__, __LINE__);
}
