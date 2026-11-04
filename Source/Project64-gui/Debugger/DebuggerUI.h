#pragma once

#include "stdafx.h"

class CDebuggerUI;

#include <Common/SyncEvent.h>
#include <Project64-core/Debugger.h>
#include <Project64/UserInterface/Debugger/Breakpoints.h>
#include <Project64/UserInterface/Debugger/CPULog.h>
#include <Project64/UserInterface/Debugger/DMALog.h>
#include <Project64/UserInterface/Debugger/DebugMMU.h>
#include <Project64/UserInterface/Debugger/Symbols.h>

#include <atomic>
#include <memory>
#include <set>
#include <string>

class CommandsView;
class CScriptSystem;
class ScriptsView;
class SymbolsView;
class StackView;
class StackTraceView;
class TlbView;
class ExceptionBreakpointsView;
class DmaLogView;
class CpuLogView;
class MemoryDumpView;
class MemoryView;
class MemorySearchView;

// Qt port of CDebuggerUI (Project64/UserInterface/Debugger/Debugger.cpp): the object
// the core talks to through CDebugger. Breakpoint/stepping/logging logic is the upstream
// one; the windows are Qt windows living in the GUI thread (see RunOnGuiThread).
//
// The JavaScript script system (upstream's CScriptSystem on duktape, see
// Project64-debugger-scripts) runs in its own thread; its <windows.h> names are a Linux
// stand-in that only that target sees, so the methods that need the script types are
// implemented in ScriptGlue.cpp (part of that target).
class CDebuggerUI :
    public CDebugger,
    public CDebugMMU
{
public:
    CDebuggerUI();
    virtual ~CDebuggerUI(void);

    void OpenCommandWindow(void) override;
    void OpenMemoryWindow(void) override;
    void OpenMemoryDump(void) override;
    void OpenMemorySearch(void) override;
    void OpenTLBWindow(void) override;
    void OpenScriptsWindow(void) override;
    void OpenSymbolsWindow(void) override;
    void OpenDMALogWindow(void) override;
    void OpenCPULogWindow(void) override;
    void OpenExcBreakpointsWindow(void) override;
    void OpenStackTraceWindow(void) override;
    void OpenStackViewWindow(void) override;
    void TLBChanged(void) override;
    void FrameDrawn(void) override;
    void WaitForStep(void) override;
    bool ExecutionBP(uint32_t address) override;
    bool ReadBP8(uint32_t address) override;
    bool ReadBP16(uint32_t address) override;
    bool ReadBP32(uint32_t address) override;
    bool ReadBP64(uint32_t address) override;
    bool WriteBP8(uint32_t address) override;
    bool WriteBP16(uint32_t address) override;
    bool WriteBP32(uint32_t address) override;
    bool WriteBP64(uint32_t address) override;
    void CPUStepStarted(void) override;
    void CPUStep(void) override;
    void CPUStepEnded(void) override;
    void PIFReadStarted(void) override;
    void RSPReceivedTask(void) override;
    void PIDMAReadStarted(void) override;
    void PIDMAWriteStarted(void) override;
    void EmulationStarted(void) override;
    void EmulationStopped(void) override;

    void Debug_Reset(void);
    // The language changed: the open windows are built again with the new texts
    void LanguageChanged(void);
    void Debug_ShowCommandsLocation(uint32_t address, bool top);
    void Debug_ShowMemoryLocation(uint32_t address, bool VAddr);
    void Debug_RefreshTLBWindow(void);
    void Debug_RefreshDMALogWindow(void);
    void Debug_RefreshCPULogWindow(void);
    void Debug_RefreshStackWindow(void);
    void Debug_RefreshStackTraceWindow(void);
    void Debug_RefreshSymbolsWindow(void);
    void CopyCommands(uint32_t address, uint32_t count);
    // Lets an emulation thread that is stopped in the debugger (WaitForStep) run again, and
    // the scripts that wait in alert() (the script system holds the lock of the emulation
    // callbacks then); called before the emulation is ended (upstream waits 20 seconds for
    // the thread and then kills it, which Linux cannot do silently)
    void ReleaseStep();

    // Scripts (upstream names; the script system calls the Debug_*ScriptsWindow ones from
    // its own thread, they only queue work for the GUI thread)
    CScriptSystem * ScriptSystem();
    ScriptsView * ScriptConsole()
    {
        return m_Scripts;
    }
    void Debug_LogScriptsWindow(const char * text);
    void Debug_ClearScriptsWindow();
    void Debug_RefreshScriptsWindow();
    void StartAutorunScripts();

    // The script system for the windows (ScriptGlue.cpp)
    enum ScriptStatus
    {
        ScriptStopped,
        ScriptStarting,
        ScriptStarted
    };
    void ScriptStart(const std::string & Name);
    void ScriptStop(const std::string & Name);
    ScriptStatus GetScriptStatus(const std::string & Name);
    void ScriptInput(const std::string & Name, const std::string & Code);
    std::string ScriptsDir();
    std::string ScriptsInstallDir();
    void ScriptConsoleClear();
    // The console text and the Debug_LogScriptsWindow sequence number it is complete up to
    std::string ScriptConsoleSnapshot(uint64_t & Sequence);
    std::set<std::string> ScriptAutorunList();
    void ScriptAutorunLoad();
    void ScriptAutorunSave(const std::set<std::string> & List);
    // The video widget's mouse (JS_HOOK_MOUSE*): kind 0 down, 1 up, 2 move; button 0 left, 1 middle, 2 right
    void ScriptMouseEvent(int Kind, int X, int Y, int Button);
    // What the script API asks of the main window (upstream WM_JSAPI_ACTION) and alert()
    void ScriptAction(unsigned Action, intptr_t Argument);
    void ScriptAlert(const char * Text, const char * Caption);
    void ReleaseScriptAlerts(bool Shutdown = false);

    CBreakpoints * Breakpoints();
    CDMALog * DMALog();
    CCPULog * CPULog();
    CSymbolTable * SymbolTable();
    SyncEvent & StepEvent();
    CommandsView * Commands()
    {
        return m_CommandsView;
    }
    // Test hooks
    StackView * Stack() { return m_StackView; }
    StackTraceView * Trace() { return m_StackTrace; }
    TlbView * Tlb() { return m_DebugTLB; }
    ExceptionBreakpointsView * ExcBreakpoints() { return m_ExcBreakpoints; }
    DmaLogView * DmaLog() { return m_DMALogView; }
    CpuLogView * CpuLog() { return m_CPULogView; }
    SymbolsView * SymbolsWindow() { return m_Symbols; }
    MemoryDumpView * Dump() { return m_MemoryDump; }
    MemoryView * Memory() { return m_MemoryView; }
    MemorySearchView * Search() { return m_MemorySearch; }

private:
    CDebuggerUI(const CDebuggerUI &);
    CDebuggerUI & operator=(const CDebuggerUI &);

    static void GameReset(CDebuggerUI * _this);
    static void SteppingOpsChanged(CDebuggerUI * _this);
    static void GameCpuRunningChanged(CDebuggerUI * _this);
    static void GameNameChanged(CDebuggerUI * _this);
    static void WaitingForStepChanged(CDebuggerUI * _this);
    static void GamePausedChanged(CDebuggerUI * _this);

    // ScriptGlue.cpp
    enum ScriptEmuState
    {
        EmuStarted,
        EmuStopped,
        EmuResetting,
        EmuReset,
        EmuPaused,
        EmuResumed,
        EmuLoadedRom,
        EmuDebugPaused,
        EmuDebugResumed
    };
    void ScriptsCreate();
    void ScriptsDestroy();
    void ScriptEmuStateChange(ScriptEmuState State);
    void ScriptCpuStep();
    void ScriptPifRead();
    void ScriptRspTask();
    void ScriptPiDma(bool ToCart);

    void HandleCPUException(void);
    void HandleCartToRamDMA(void);

    // Window pointers are only touched from the GUI thread (the windows are top-level).
    CommandsView * m_CommandsView = nullptr;
    SymbolsView * m_Symbols = nullptr;
    StackView * m_StackView = nullptr;
    StackTraceView * m_StackTrace = nullptr;
    TlbView * m_DebugTLB = nullptr;
    ExceptionBreakpointsView * m_ExcBreakpoints = nullptr;
    DmaLogView * m_DMALogView = nullptr;
    std::atomic<bool> m_DmaRefreshQueued{false};
    CpuLogView * m_CPULogView = nullptr;
    MemoryDumpView * m_MemoryDump = nullptr;
    MemoryView * m_MemoryView = nullptr;
    MemorySearchView * m_MemorySearch = nullptr;
    ScriptsView * m_Scripts = nullptr;
    CScriptSystem * m_ScriptSystem = nullptr;

    // The script system's thread and the emulation thread queue work for the GUI thread with
    // pointers to this object: the flag tells that work that the object is gone
    std::shared_ptr<std::atomic<bool>> m_Alive = std::make_shared<std::atomic<bool>>(true);
    std::atomic<uint64_t> m_ScriptLogSequence{0};
    // alert() blocks the calling script (the script system then holds the lock of the
    // emulation callbacks) until its box is closed, but not while the UI ends a running
    // emulation (from ReleaseStep until EmulationStopped): the UI waits for the emulation
    // thread, which waits for the lock
    std::atomic<bool> m_EmulationClosing{false};
    std::atomic<bool> m_ScriptsShutdown{false};

    CBreakpoints * m_Breakpoints;
    CDMALog * m_DMALog;
    CCPULog * m_CPULog;
    CSymbolTable * m_SymbolTable;
    SyncEvent m_StepEvent;
};

// Called before ending the emulation from the UI: a thread stopped in the debugger would
// otherwise keep the emulation from closing
inline void DebuggerReleaseStep()
{
    if (g_Debugger != nullptr && g_DebugSettings.haveDebugger)
    {
        static_cast<CDebuggerUI *>(g_Debugger)->ReleaseStep();
    }
}
