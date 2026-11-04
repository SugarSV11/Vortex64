// The part of CDebuggerUI (Project64/UserInterface/Debugger/Debugger.cpp) that talks to the
// script system. It is compiled with the script engine (Project64-debugger-scripts) because it
// needs CScriptSystem and the hook types, whose <windows.h> names are the ScriptShim stand-in.
#include "stdafx.h"

#include "DebuggerUI.h"

#include <atomic>
#include <chrono>
#include <thread>

#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Settings/DebugSettings.h>
#include <Project64/UserInterface/Debugger/OpInfo.h>
#include <Project64/UserInterface/Debugger/ScriptSystem.h>
#include <Project64/UserInterface/Debugger/ScriptTypes.h>
#include <Project64/UserInterface/MainWindow.h> // ScriptShim: WM_JSAPI_ACTION

namespace
{
// The script API posts to the main window of upstream (WM_JSAPI_ACTION) and shows message boxes
// (alert): the frontend handles both
void ScriptPostMessage(unsigned Message, uintptr_t WParam, intptr_t LParam)
{
    if (Message == WM_JSAPI_ACTION && g_Debugger != nullptr)
    {
        static_cast<CDebuggerUI *>(g_Debugger)->ScriptAction((unsigned)WParam, LParam);
    }
}

void ScriptMessageBox(const char * Text, const char * Caption)
{
    if (g_Debugger != nullptr)
    {
        static_cast<CDebuggerUI *>(g_Debugger)->ScriptAlert(Text, Caption);
    }
}
} // namespace

void CDebuggerUI::ScriptsCreate()
{
    g_ScriptShimHooks.PostMessage = ScriptPostMessage;
    g_ScriptShimHooks.MessageBox = ScriptMessageBox;
    m_ScriptSystem = new CScriptSystem(this);
}

// The script thread ends its scripts (stopping their timers, servers, sockets) before it exits
void CDebuggerUI::ScriptsDestroy()
{
    ReleaseScriptAlerts(true);
    // A script waiting for a child process (exec) cannot end until the child does: the processes
    // that scripts start are ended, including those that a script starts while it shuts down
    std::atomic<bool> Done{false};
    std::thread Killer([&Done]() {
        while (!Done)
        {
            ScriptShimTerminateProcesses();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    delete m_ScriptSystem;
    m_ScriptSystem = nullptr;
    Done = true;
    Killer.join();
    // once more: the thread may not have run before the script system was gone
    ScriptShimTerminateProcesses();
    g_ScriptShimHooks.PostMessage = nullptr;
    g_ScriptShimHooks.MessageBox = nullptr;
}

CScriptSystem * CDebuggerUI::ScriptSystem()
{
    return m_ScriptSystem;
}

void CDebuggerUI::StartAutorunScripts(void)
{
    if (m_ScriptSystem == nullptr)
    {
        return;
    }

    m_ScriptSystem->ExecAutorunList();
}

void CDebuggerUI::ScriptEmuStateChange(ScriptEmuState State)
{
    JSHookEmuStateChangeEnv env;
    switch (State)
    {
    case EmuStarted: env.state = JS_EMU_STARTED; break;
    case EmuStopped: env.state = JS_EMU_STOPPED; break;
    case EmuResetting: env.state = JS_EMU_RESETTING; break;
    case EmuReset: env.state = JS_EMU_RESET; break;
    case EmuPaused: env.state = JS_EMU_PAUSED; break;
    case EmuResumed: env.state = JS_EMU_RESUMED; break;
    case EmuLoadedRom: env.state = JS_EMU_LOADED_ROM; break;
    case EmuDebugPaused: env.state = JS_EMU_DEBUG_PAUSED; break;
    case EmuDebugResumed: env.state = JS_EMU_DEBUG_RESUMED; break;
    default: return;
    }
    m_ScriptSystem->InvokeAppCallbacks(JS_HOOK_EMUSTATECHANGE, &env);
}

// The script part of CDebuggerUI::CPUStepStarted
void CDebuggerUI::ScriptCpuStep()
{
    if (m_ScriptSystem->HaveAppCallbacks())
    {
        JSHookCpuStepEnv hookEnv;
        hookEnv.pc = (uint32_t)g_Reg->m_PROGRAM_COUNTER;
        hookEnv.opInfo = COpInfo(g_System->Opcode());

        if (m_ScriptSystem->HaveCpuExecCallbacks(hookEnv.pc))
        {
            m_ScriptSystem->InvokeAppCallbacks(JS_HOOK_CPU_EXEC, (void *)&hookEnv);
        }

        if (hookEnv.opInfo.IsLoadCommand() && m_ScriptSystem->HaveCpuReadCallbacks(hookEnv.opInfo.GetLoadStoreAddress()))
        {
            m_ScriptSystem->InvokeAppCallbacks(JS_HOOK_CPU_READ, (void *)&hookEnv);
        }
        else if (hookEnv.opInfo.IsStoreCommand() && m_ScriptSystem->HaveCpuWriteCallbacks(hookEnv.opInfo.GetLoadStoreAddress()))
        {
            m_ScriptSystem->InvokeAppCallbacks(JS_HOOK_CPU_WRITE, (void *)&hookEnv);
        }
    }
}

void CDebuggerUI::ScriptPifRead()
{
    m_ScriptSystem->InvokeAppCallbacks(JS_HOOK_PIFREAD);
}

void CDebuggerUI::ScriptRspTask()
{
    JSHookSpTaskEnv env;

    DebugLoad_VAddr(0xA4000FC0, env.taskType);
    DebugLoad_VAddr(0xA4000FC4, env.taskFlags);
    DebugLoad_VAddr(0xA4000FC8, env.ucodeBootAddress);
    DebugLoad_VAddr(0xA4000FCC, env.ucodeBootSize);
    DebugLoad_VAddr(0xA4000FD0, env.ucodeAddress);
    DebugLoad_VAddr(0xA4000FD4, env.ucodeSize);
    DebugLoad_VAddr(0xA4000FD8, env.ucodeDataAddress);
    DebugLoad_VAddr(0xA4000FDC, env.ucodeDataSize);
    DebugLoad_VAddr(0xA4000FE0, env.dramStackAddress);
    DebugLoad_VAddr(0xA4000FE4, env.dramStackSize);
    DebugLoad_VAddr(0xA4000FE8, env.outputBuffAddress);
    DebugLoad_VAddr(0xA4000FEC, env.outputBuffSize);
    DebugLoad_VAddr(0xA4000FF0, env.dataAddress);
    DebugLoad_VAddr(0xA4000FF4, env.dataSize);
    DebugLoad_VAddr(0xA4000FF8, env.yieldDataAddress);
    DebugLoad_VAddr(0xA4000FFC, env.yieldDataSize);

    m_ScriptSystem->InvokeAppCallbacks(JS_HOOK_RSPTASK, &env);
}

void CDebuggerUI::ScriptPiDma(bool ToCart)
{
    JSHookPiDmaEnv env;

    env.direction = ToCart ? 1 : 0;
    DebugLoad_VAddr(0xA4600000, env.dramAddress);
    DebugLoad_VAddr(0xA4600004, env.cartAddress);
    DebugLoad_VAddr(ToCart ? 0xA4600008 : 0xA460000C, env.length);

    m_ScriptSystem->InvokeAppCallbacks(JS_HOOK_PIDMA, &env);
}

void CDebuggerUI::ScriptMouseEvent(int Kind, int X, int Y, int Button)
{
    if (m_ScriptSystem == nullptr)
    {
        return;
    }
    // the WM_* message of upstream's WndProc
    static const DWORD Messages[3][3] = {
        {WM_LBUTTONDOWN, WM_MBUTTONDOWN, WM_RBUTTONDOWN},
        {WM_LBUTTONUP, WM_MBUTTONUP, WM_RBUTTONUP},
        {(DWORD)-1, (DWORD)-1, (DWORD)-1},
    };
    if (Kind < 0 || Kind > 2 || Button < 0 || Button > 2)
    {
        return;
    }
    static const JSAppHookID Hooks[3] = {JS_HOOK_MOUSEDOWN, JS_HOOK_MOUSEUP, JS_HOOK_MOUSEMOVE};
    m_ScriptSystem->DoMouseEvent(Hooks[Kind], X, Y, Messages[Kind][Button]);
}

// ---- for the windows ----

void CDebuggerUI::ScriptStart(const std::string & Name)
{
    m_ScriptSystem->StartScript(Name.c_str(), (m_ScriptSystem->ScriptsDirPath() + Name).c_str());
}

void CDebuggerUI::ScriptStop(const std::string & Name)
{
    m_ScriptSystem->StopScript(Name.c_str());
}

CDebuggerUI::ScriptStatus CDebuggerUI::GetScriptStatus(const std::string & Name)
{
    switch (m_ScriptSystem->GetStatus(Name.c_str()))
    {
    case JS_STATUS_STARTING: return ScriptStarting;
    case JS_STATUS_STARTED: return ScriptStarted;
    default: return ScriptStopped;
    }
}

void CDebuggerUI::ScriptInput(const std::string & Name, const std::string & Code)
{
    m_ScriptSystem->Input(Name.c_str(), Code.c_str());
}

std::string CDebuggerUI::ScriptsDir()
{
    return m_ScriptSystem->ScriptsDirPath();
}

std::string CDebuggerUI::ScriptsInstallDir()
{
    return m_ScriptSystem->InstallDirPath();
}

void CDebuggerUI::ScriptConsoleClear()
{
    m_ScriptSystem->ConsoleClear();
}

std::string CDebuggerUI::ScriptConsoleSnapshot(uint64_t & Sequence)
{
    // the console text is appended together with the sequence number in ConsoleLog (under the
    // same lock as GetConsoleBuffer), so a sequence number that did not change while the text
    // was read belongs to that text
    for (;;)
    {
        const uint64_t Before = m_ScriptLogSequence;
        std::string Text = m_ScriptSystem->GetConsoleBuffer();
        if (Before == m_ScriptLogSequence)
        {
            Sequence = Before;
            return Text;
        }
    }
}

std::set<std::string> CDebuggerUI::ScriptAutorunList()
{
    return m_ScriptSystem->AutorunList();
}

void CDebuggerUI::ScriptAutorunLoad()
{
    m_ScriptSystem->LoadAutorunList();
}

void CDebuggerUI::ScriptAutorunSave(const std::set<std::string> & List)
{
    m_ScriptSystem->AutorunList() = List;
    m_ScriptSystem->SaveAutorunList();
}
