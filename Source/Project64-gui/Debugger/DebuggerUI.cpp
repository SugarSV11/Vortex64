#include "stdafx.h"
#include "DebuggerUI.h"
#include "CommandsView.h"
#include "CpuLogView.h"
#include "DmaLogView.h"
#include "ExceptionBreakpointsView.h"
#include "MemoryDumpView.h"
#include "MemorySearchView.h"
#include "MemoryView.h"
#include "TlbView.h"
#include "StackWindows.h"
#include "SymbolsView.h"
#include "DebugCommon.h"
#include "ScriptsView.h"
#include "ScriptShim/Project64/UserInterface/MainWindow.h" // WM_JSAPI_ACTION and JSAPI_ACT_*
#include "../MainWindow.h"
#include <Project64/UserInterface/DiscordRPC.h>

#include <Project64-core/N64System/Mips/R4300iInstruction.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Settings/DebugSettings.h>
#include <Project64/UserInterface/Debugger/OpInfo.h>

#include <QApplication>
#include <QMessageBox>
#include <QPointer>
#include <chrono>
#include <condition_variable>
#include <mutex>

// Qt port of Project64/UserInterface/Debugger/Debugger.cpp. The break/step/log logic is
// upstream's; only the window management differs (Qt windows in the GUI thread).

CDebuggerUI::CDebuggerUI() :
    m_Breakpoints(nullptr),
    m_DMALog(nullptr),
    m_CPULog(nullptr),
    m_SymbolTable(nullptr),
    m_StepEvent(false)
{
    g_Debugger = this;

    m_Breakpoints = new CBreakpoints();
    ScriptsCreate();
    m_DMALog = new CDMALog();
    m_CPULog = new CCPULog();
    m_SymbolTable = new CSymbolTable(this);

    g_Settings->RegisterChangeCB(GameRunning_InReset, this, (CSettings::SettingChangedFunc)GameReset);
    g_Settings->RegisterChangeCB(Debugger_SteppingOps, this, (CSettings::SettingChangedFunc)SteppingOpsChanged);
    g_Settings->RegisterChangeCB(GameRunning_CPU_Running, this, (CSettings::SettingChangedFunc)GameCpuRunningChanged);
    g_Settings->RegisterChangeCB(Game_GameName, this, (CSettings::SettingChangedFunc)GameNameChanged);
    g_Settings->RegisterChangeCB(GameRunning_CPU_Paused, this, (CSettings::SettingChangedFunc)GamePausedChanged);
    g_Settings->RegisterChangeCB(Debugger_WaitingForStep, this, (CSettings::SettingChangedFunc)WaitingForStepChanged);
}

CDebuggerUI::~CDebuggerUI(void)
{
    g_Settings->UnregisterChangeCB(Debugger_SteppingOps, this, (CSettings::SettingChangedFunc)SteppingOpsChanged);
    g_Settings->UnregisterChangeCB(GameRunning_InReset, this, (CSettings::SettingChangedFunc)GameReset);
    // upstream registers GameRunning_CPU_Running a second time here instead of unregistering it
    g_Settings->UnregisterChangeCB(GameRunning_CPU_Running, this, (CSettings::SettingChangedFunc)GameCpuRunningChanged);
    g_Settings->UnregisterChangeCB(Game_GameName, this, (CSettings::SettingChangedFunc)GameNameChanged);
    g_Settings->UnregisterChangeCB(GameRunning_CPU_Paused, this, (CSettings::SettingChangedFunc)GamePausedChanged);
    g_Settings->UnregisterChangeCB(Debugger_WaitingForStep, this, (CSettings::SettingChangedFunc)WaitingForStepChanged);
    // Work queued for the GUI thread by the script system or the emulation must not reach a
    // destroyed object; the script system (and with it every script) ends before the windows
    *m_Alive = false;
    ScriptsDestroy();
    Debug_Reset();
    delete m_Breakpoints;
    delete m_DMALog;
    delete m_CPULog;
    delete m_SymbolTable;
    if (g_Debugger == this)
    {
        g_Debugger = nullptr;
    }
}

void CDebuggerUI::SteppingOpsChanged(CDebuggerUI * _this)
{
    if (g_Settings->LoadBool(Debugger_SteppingOps))
    {
        if (!g_Settings->LoadBool(Debugger_SilentBreak))
        {
            _this->OpenCommandWindow();
        }
        g_Settings->SaveBool(Debugger_SilentBreak, false);
    }
}

void CDebuggerUI::GameCpuRunningChanged(CDebuggerUI * _this)
{
    if (!g_Settings->LoadBool(GameRunning_CPU_Running))
    {
        RunOnGuiThread([_this, Alive = _this->m_Alive]() {
            if (*Alive && _this->m_MemorySearch)
            {
                _this->m_MemorySearch->GameReset();
            }
        });
    }
}

void CDebuggerUI::GameNameChanged(CDebuggerUI * _this)
{
    RunOnGuiThread([_this, Alive = _this->m_Alive]() {
        if (*Alive && _this->m_MemorySearch)
        {
            _this->m_MemorySearch->GameReset();
        }
    });
    _this->ScriptEmuStateChange(EmuLoadedRom);
}

void CDebuggerUI::GamePausedChanged(CDebuggerUI * _this)
{
    _this->ScriptEmuStateChange(g_Settings->LoadBool(GameRunning_CPU_Paused) ? EmuPaused : EmuResumed);
}

void CDebuggerUI::WaitingForStepChanged(CDebuggerUI * _this)
{
    _this->ScriptEmuStateChange(g_Settings->LoadBool(Debugger_WaitingForStep) ? EmuDebugPaused : EmuDebugResumed);
}

void CDebuggerUI::GameReset(CDebuggerUI * _this)
{
    if (!g_Settings->LoadBool(GameRunning_InReset))
    {
        _this->ScriptEmuStateChange(EmuReset);
        return;
    }
    _this->ScriptEmuStateChange(EmuResetting);
    RunOnGuiThread([_this, Alive = _this->m_Alive]() {
        if (!*Alive)
        {
            return;
        }
        if (_this->m_CommandsView)
        {
            _this->m_CommandsView->Reset();
        }
        if (_this->m_DMALogView)
        {
            _this->m_DMALogView->RefreshDMALogWindow(true);
        }
        if (_this->m_StackTrace)
        {
            _this->m_StackTrace->ClearEntries();
        }
        if (_this->m_SymbolTable)
        {
            _this->m_SymbolTable->Load();
        }
        if (_this->m_Symbols)
        {
            _this->m_Symbols->Refresh();
        }
    });
}

// Windows are only destroyed from the GUI thread
void CDebuggerUI::Debug_Reset(void)
{
    auto Delete = [](auto *& Window) {
        if (Window)
        {
            Window->HideWindow();
            delete Window;
            Window = nullptr;
        }
    };
    Delete(m_CommandsView);
    Delete(m_Symbols);
    Delete(m_StackView);
    Delete(m_StackTrace);
    Delete(m_DebugTLB);
    Delete(m_ExcBreakpoints);
    Delete(m_DMALogView);
    Delete(m_CPULogView);
    Delete(m_MemoryDump);
    Delete(m_MemoryView);
    Delete(m_MemorySearch);
    Delete(m_Scripts);
}

void CDebuggerUI::LanguageChanged(void)
{
    // what is open (a window in the tabs of the debugger is open when it is embedded) and which one is the one in front
    struct View
    {
        DebugWindow * Window;
        void (CDebuggerUI::*Open)(void);
    };
    const View Views[] = {{m_CommandsView, &CDebuggerUI::OpenCommandWindow}, {m_MemoryView, &CDebuggerUI::OpenMemoryWindow}, {m_MemorySearch, &CDebuggerUI::OpenMemorySearch},
                          {m_MemoryDump, &CDebuggerUI::OpenMemoryDump}, {m_Symbols, &CDebuggerUI::OpenSymbolsWindow}, {m_StackView, &CDebuggerUI::OpenStackViewWindow},
                          {m_StackTrace, &CDebuggerUI::OpenStackTraceWindow}, {m_DebugTLB, &CDebuggerUI::OpenTLBWindow}, {m_ExcBreakpoints, &CDebuggerUI::OpenExcBreakpointsWindow},
                          {m_DMALogView, &CDebuggerUI::OpenDMALogWindow}, {m_CPULogView, &CDebuggerUI::OpenCPULogWindow}, {m_Scripts, &CDebuggerUI::OpenScriptsWindow}};
    bool Was[sizeof(Views) / sizeof(Views[0])];
    int Front = -1;
    for (size_t i = 0; i < sizeof(Views) / sizeof(Views[0]); i++)
    {
        Was[i] = Views[i].Window != nullptr && (Views[i].Window->isVisible() || Views[i].Window->Embedded());
        if (Views[i].Window != nullptr && Views[i].Window->isVisible())
        {
            Front = (int)i;
        }
    }
    Debug_Reset();
    for (size_t i = 0; i < sizeof(Views) / sizeof(Views[0]); i++)
    {
        if (Was[i])
        {
            (this->*Views[i].Open)();
        }
    }
    if (Front >= 0)
    {
        (this->*Views[Front].Open)();
    }
}

void CDebuggerUI::ReleaseScriptAlerts(bool Shutdown)
{
    if (Shutdown)
    {
        m_ScriptsShutdown = true;
    }
    else if (g_Settings->LoadBool(GameRunning_CPU_Running))
    {
        m_EmulationClosing = true;
    }
    RunOnGuiThread([]() {
        for (QWidget * Widget : QApplication::topLevelWidgets())
        {
            if (QMessageBox * Box = qobject_cast<QMessageBox *>(Widget))
            {
                if (Box->testAttribute(Qt::WA_DeleteOnClose))
                {
                    Box->close();
                }
            }
        }
    });
}

// The Open*Window calls come from the menu (GUI thread) or from the emulation thread
// (breakpoints); the window is created and shown in the GUI thread.
void CDebuggerUI::OpenCommandWindow()
{
    RunOnGuiThread([this]() {
        if (m_CommandsView == nullptr)
        {
            m_CommandsView = new CommandsView(this, m_StepEvent);
        }
        m_CommandsView->ShowWindow();
    });
}

void CDebuggerUI::Debug_ShowCommandsLocation(uint32_t address, bool top)
{
    OpenCommandWindow();
    RunOnGuiThread([this, address, top]() {
        if (m_CommandsView)
        {
            m_CommandsView->ShowAddress(address, top);
        }
    });
}

void CDebuggerUI::OpenMemoryWindow(void)
{
    RunOnGuiThread([this]() {
        if (m_MemoryView == nullptr)
        {
            m_MemoryView = new MemoryView(this);
        }
        m_MemoryView->ShowWindow();
    });
}
void CDebuggerUI::OpenMemoryDump(void)
{
    RunOnGuiThread([this]() {
        if (g_MMU == nullptr)
        {
            return;
        }
        if (m_MemoryDump == nullptr)
        {
            m_MemoryDump = new MemoryDumpView(this);
        }
        m_MemoryDump->ShowWindow();
    });
}
void CDebuggerUI::OpenMemorySearch(void)
{
    RunOnGuiThread([this]() {
        if (m_MemorySearch == nullptr)
        {
            m_MemorySearch = new MemorySearchView(this);
        }
        m_MemorySearch->ShowWindow();
    });
}
void CDebuggerUI::OpenTLBWindow(void)
{
    RunOnGuiThread([this]() {
        if (g_MMU == nullptr)
        {
            return;
        }
        if (m_DebugTLB == nullptr)
        {
            m_DebugTLB = new TlbView(this);
        }
        m_DebugTLB->ShowWindow();
    });
}
void CDebuggerUI::OpenScriptsWindow(void)
{
    RunOnGuiThread([this]() {
        if (m_Scripts == nullptr)
        {
            m_Scripts = new ScriptsView(this);
        }
        m_Scripts->ShowWindow();
    });
}

// Called by the script system (its thread, or the thread that starts the autorun scripts)
// with its console lock held: only queue the work. The sequence number orders the text
// against the console snapshot a new Scripts window starts from (ScriptConsoleSnapshot).
void CDebuggerUI::Debug_LogScriptsWindow(const char * text)
{
    const uint64_t Sequence = ++m_ScriptLogSequence;
    RunOnGuiThread([this, Alive = m_Alive, Sequence, Text = std::string(text)]() {
        if (*Alive && m_Scripts != nullptr)
        {
            m_Scripts->ConsolePrint(Sequence, Text);
        }
    });
}

void CDebuggerUI::Debug_ClearScriptsWindow()
{
    const uint64_t Sequence = ++m_ScriptLogSequence;
    RunOnGuiThread([this, Alive = m_Alive, Sequence]() {
        if (*Alive && m_Scripts != nullptr)
        {
            m_Scripts->ConsoleClear(Sequence);
        }
    });
}

void CDebuggerUI::Debug_RefreshScriptsWindow()
{
    RunOnGuiThread([this, Alive = m_Alive]() {
        if (*Alive && m_Scripts != nullptr)
        {
            m_Scripts->RefreshList();
        }
    });
}

// WM_JSAPI_ACTION of the main window (Project64/UserInterface/MainWindow.cpp), in the GUI thread
void CDebuggerUI::ScriptAction(unsigned Action, intptr_t Argument)
{
    // always queued, like the PostMessage it stands for: the script may be running in the GUI
    // thread (the mouse hooks) holding the lock of the emulation callbacks, which ending or
    // changing the emulation would wait for
    QCoreApplication * App = QCoreApplication::instance();
    if (App == nullptr)
    {
        if (Action == JSAPI_ACT_OPEN_ROM)
        {
            delete[] (char *)Argument;
        }
        return;
    }
    QMetaObject::invokeMethod(App, [this, Alive = m_Alive, Action, Argument]() {
        if (Action == JSAPI_ACT_OPEN_ROM)
        {
            // allocated by the script API
            char * RomPath = (char *)Argument;
            if (*Alive)
            {
                for (QWidget * Widget : QApplication::topLevelWidgets())
                {
                    if (MainWindow * Window = qobject_cast<MainWindow *>(Widget))
                    {
                        Window->OpenImage(QString::fromLocal8Bit(RomPath));
                        break;
                    }
                }
            }
            delete[] RomPath;
            return;
        }
        if (!*Alive || g_BaseSystem == nullptr)
        {
            return;
        }
        switch (Action)
        {
        case JSAPI_ACT_CLOSE_ROM:
            ReleaseStep();
            g_BaseSystem->ExternalEvent(SysEvent_CloseCPU);
            if (UISettingsLoadBool(Setting_EnableDiscordRPC))
            {
                CDiscord::Update(false);
            }
            break;
        case JSAPI_ACT_RESET:
            g_BaseSystem->ExternalEvent(Argument != 0 ? SysEvent_ResetCPU_Soft : SysEvent_ResetCPU_Hard);
            break;
        case JSAPI_ACT_PAUSE:
            g_BaseSystem->ExternalEvent(SysEvent_PauseCPU_FromMenu);
            break;
        case JSAPI_ACT_RESUME:
            g_BaseSystem->ExternalEvent(SysEvent_ResumeCPU_FromMenu);
            break;
        }
    }, Qt::QueuedConnection);
}

// alert(): the calling script waits until the box is closed, like MessageBox does (it does not
// wait while the emulation is being ended or the scripts are shut down, see ReleaseScriptAlerts)
void CDebuggerUI::ScriptAlert(const char * Text, const char * Caption)
{
    // A callback that the GUI thread itself runs (the mouse hooks): MessageBox of Windows runs a
    // message loop for the box in that thread, and so does exec()
    QCoreApplication * App = QCoreApplication::instance();
    if (App != nullptr && QThread::currentThread() == App->thread())
    {
        QMessageBox Box(QMessageBox::NoIcon, QString::fromUtf8(Caption), QString::fromUtf8(Text), QMessageBox::Ok);
        Box.exec();
        return;
    }

    struct Wait
    {
        std::mutex Lock;
        std::condition_variable Changed;
        bool Done = false;
    };
    auto Pending = std::make_shared<Wait>();
    auto Finish = [Pending]() {
        std::lock_guard<std::mutex> Guard(Pending->Lock);
        Pending->Done = true;
        Pending->Changed.notify_all();
    };
    RunOnGuiThread([this, Alive = m_Alive, Finish, Text = QString::fromUtf8(Text), Caption = QString::fromUtf8(Caption)]() {
        if (!*Alive)
        {
            Finish();
            return;
        }
        // modal like upstream's MessageBox on the main window: the main window cannot end the
        // emulation (which the script system's callback lock would block) while it is open
        QMessageBox * Box = new QMessageBox(QMessageBox::NoIcon, Caption, Text, QMessageBox::Ok);
        Box->setAttribute(Qt::WA_DeleteOnClose);
        QObject::connect(Box, &QObject::destroyed, [Finish]() { Finish(); });
        Box->setWindowModality(m_EmulationClosing ? Qt::NonModal : Qt::ApplicationModal);
        Box->show();
    });
    std::unique_lock<std::mutex> Guard(Pending->Lock);
    while (!Pending->Done && !m_ScriptsShutdown && !m_EmulationClosing)
    {
        Pending->Changed.wait_for(Guard, std::chrono::milliseconds(50));
    }
}
void CDebuggerUI::OpenSymbolsWindow(void)
{
    RunOnGuiThread([this]() {
        if (m_Symbols == nullptr)
        {
            m_Symbols = new SymbolsView(this);
        }
        m_Symbols->ShowWindow();
    });
}

void CDebuggerUI::OpenDMALogWindow(void)
{
    RunOnGuiThread([this]() {
        if (m_DMALogView == nullptr)
        {
            m_DMALogView = new DmaLogView(this);
        }
        m_DMALogView->ShowWindow();
    });
}

void CDebuggerUI::OpenCPULogWindow(void)
{
    RunOnGuiThread([this]() {
        if (m_CPULogView == nullptr)
        {
            m_CPULogView = new CpuLogView(this);
        }
        m_CPULogView->ShowWindow();
    });
}

void CDebuggerUI::OpenExcBreakpointsWindow(void)
{
    RunOnGuiThread([this]() {
        if (m_ExcBreakpoints == nullptr)
        {
            m_ExcBreakpoints = new ExceptionBreakpointsView(this);
        }
        m_ExcBreakpoints->ShowWindow();
    });
}
void CDebuggerUI::OpenStackTraceWindow(void)
{
    RunOnGuiThread([this]() {
        if (m_StackTrace == nullptr)
        {
            m_StackTrace = new StackTraceView(this);
            g_Settings->SaveBool(Debugger_TrackCPUStepEnded, true);
        }
        m_StackTrace->ShowWindow();
    });
}

void CDebuggerUI::OpenStackViewWindow(void)
{
    RunOnGuiThread([this]() {
        if (m_StackView == nullptr)
        {
            m_StackView = new StackView(this);
        }
        m_StackView->ShowWindow();
    });
}

void CDebuggerUI::Debug_ShowMemoryLocation(uint32_t Address, bool VAddr)
{
    OpenMemoryWindow();
    RunOnGuiThread([this, Address, VAddr]() {
        if (m_MemoryView)
        {
            m_MemoryView->ShowAddress(Address, VAddr);
        }
    });
}
void CDebuggerUI::Debug_RefreshTLBWindow(void)
{
    RunOnGuiThread([this]() {
        if (m_DebugTLB)
        {
            m_DebugTLB->RefreshTLBWindow();
        }
    });
}

void CDebuggerUI::Debug_RefreshDMALogWindow(void)
{
    // (called by the emulation thread at every cartridge DMA; the window is created and deleted by the GUI thread, so the
    // pointer is read there, and a refresh already queued covers the ones that follow)
    if (m_DmaRefreshQueued.exchange(true))
    {
        return;
    }
    RunOnGuiThread([this]() {
        m_DmaRefreshQueued = false;
        if (m_DMALogView != nullptr)
        {
            m_DMALogView->RefreshDMALogWindow();
        }
    });
}

void CDebuggerUI::Debug_RefreshCPULogWindow(void)
{
    RunOnGuiThread([this]() {
        if (m_CPULogView != nullptr)
        {
            m_CPULogView->RefreshList();
        }
    });
}
void CDebuggerUI::Debug_RefreshStackWindow(void)
{
    RunOnGuiThread([this]() {
        if (m_StackView != nullptr)
        {
            m_StackView->Refresh();
        }
    });
}

void CDebuggerUI::Debug_RefreshStackTraceWindow(void)
{
    RunOnGuiThread([this]() {
        if (m_StackTrace != nullptr)
        {
            m_StackTrace->Refresh();
        }
    });
}

void CDebuggerUI::Debug_RefreshSymbolsWindow(void)
{
    RunOnGuiThread([this]() {
        if (m_Symbols != nullptr)
        {
            m_Symbols->Refresh();
        }
    });
}

CBreakpoints * CDebuggerUI::Breakpoints()
{
    return m_Breakpoints;
}

CDMALog * CDebuggerUI::DMALog()
{
    return m_DMALog;
}

CCPULog * CDebuggerUI::CPULog()
{
    return m_CPULog;
}

CSymbolTable * CDebuggerUI::SymbolTable()
{
    return m_SymbolTable;
}

SyncEvent & CDebuggerUI::StepEvent()
{
    return m_StepEvent;
}

void CDebuggerUI::TLBChanged()
{
    Debug_RefreshTLBWindow();
}

// Exception handling - break on exception vector if exception breakpoint is set
void CDebuggerUI::HandleCPUException(void)
{
    int exc = (g_Reg->CAUSE_REGISTER.Value >> 2) & 0x1F;
    int intr = (g_Reg->CAUSE_REGISTER.Value >> 8) & 0xFF;
    int fpExc = (g_Reg->m_FPCR[31] >> 12) & 0x3F;
    int rcpIntr = g_Reg->MI_INTR_REG & 0x2F;

    if ((g_DebugSettings.exceptionBreakpoints & (1 << exc)))
    {
        if (exc == 15) // Floating-point exception
        {
            if (fpExc & g_DebugSettings.fpExceptionBreakpoints)
            {
                goto have_bp;
            }
            return;
        }
        else if (exc == 0) // Interrupt exception
        {
            if (intr & g_DebugSettings.intrBreakpoints)
            {
                if (intr & 0x04) // RCP interrupt (IP2)
                {
                    if (rcpIntr & g_DebugSettings.rcpIntrBreakpoints)
                    {
                        goto have_bp;
                    }
                    return;
                }
                else // Other interrupts
                {
                    goto have_bp;
                }
            }
            return;
        }
        else // Other exceptions
        {
            goto have_bp;
        }
    }

    return;

have_bp:
    if (g_DebugSettings.cpuLoggingEnabled)
    {
        g_Debugger->OpenCPULogWindow();
    }

    g_Settings->SaveBool(Debugger_SteppingOps, true);
}

void CDebuggerUI::HandleCartToRamDMA(void)
{
    COpInfo opInfo(g_System->Opcode());

    uint32_t dmaRomAddr = g_Reg->PI_CART_ADDR_REG & 0x0FFFFFFF;
    uint32_t dmaRamAddr = g_Reg->PI_DRAM_ADDR_REG | 0x80000000;
    uint32_t dmaLen = opInfo.GetStoreValueUnsigned() + 1;

    m_DMALog->AddEntry(dmaRomAddr, dmaRamAddr, dmaLen);
    Debug_RefreshDMALogWindow();

    // Break if write breakpoint exists anywhere in target buffer
    if (m_Breakpoints->WriteBPExistsInChunk(dmaRamAddr, dmaLen))
    {
        g_Settings->SaveBool(Debugger_SteppingOps, true);
    }
}

// Called from the interpreter core at the beginning of every CPU step
void CDebuggerUI::CPUStepStarted()
{
    if (g_DebugSettings.stepping && g_DebugSettings.cpuLoggingEnabled)
    {
        Debug_RefreshCPULogWindow();
    }

    if (m_Breakpoints->NumMemLocks() > 0)
    {
        COpInfo opInfo(g_System->Opcode());
        bool bStoreOp = opInfo.IsStoreCommand();

        if (bStoreOp)
        {
            uint32_t storeAddress = bStoreOp ? opInfo.GetLoadStoreAddress() : 0;
            if (m_Breakpoints->MemLockExists(storeAddress, opInfo.NumBytesToStore()))
            {
                // Memory is locked, skip op
                g_Settings->SaveBool(Debugger_SkipOp, true);
                return;
            }
        }
    }

    ScriptCpuStep();

    if (g_DebugSettings.exceptionBreakpoints != 0)
    {
        uint32_t pc = (uint32_t)g_Reg->m_PROGRAM_COUNTER;

        if (pc == 0x80000000 || pc == 0x80000080 || pc == 0xA0000100 || pc == 0x80000180)
        {
            if (g_Reg->STATUS_REGISTER.ExceptionLevel != 0 || g_Reg->STATUS_REGISTER.ErrorLevel != 0) // If EXL/ERL bits are set
            {
                HandleCPUException();
            }
        }
    }

    if (m_Breakpoints->HaveRegBP())
    {
        R4300iInstruction opInfo(g_Reg->m_PROGRAM_COUNTER, g_System->Opcode().Value);

        if (m_Breakpoints->HaveAnyGPRWriteBP())
        {
            uint32_t nReg = opInfo.WritesGPR();
            if (nReg != 0 && m_Breakpoints->HaveGPRWriteBP(nReg))
            {
                g_Settings->SaveBool(Debugger_SteppingOps, true);
            }
        }

        if (m_Breakpoints->HaveAnyGPRReadBP())
        {
            uint32_t nReg1 = 0, nReg2 = 0;
            opInfo.ReadsGPR(nReg1, nReg2);

            if ((nReg1 != 0 && m_Breakpoints->HaveGPRReadBP(nReg1)) || (nReg2 != 0 && m_Breakpoints->HaveGPRReadBP(nReg2)))
            {
                g_Settings->SaveBool(Debugger_SteppingOps, true);
            }
        }

        if (m_Breakpoints->HaveHIWriteBP() && opInfo.WritesHI() || m_Breakpoints->HaveLOWriteBP() && opInfo.WritesLO() || m_Breakpoints->HaveHIReadBP() && opInfo.ReadsHI() || m_Breakpoints->HaveLOReadBP() && opInfo.ReadsLO())
        {
            g_Settings->SaveBool(Debugger_SteppingOps, true);
        }
    }
}

// Called before opcode is executed (not called if SkipOp is set)
void CDebuggerUI::CPUStep()
{
    if (g_DebugSettings.cpuLoggingEnabled)
    {
        m_CPULog->PushState();
    }
}

// Called after opcode has been executed
// Called after opcode has been executed
void CDebuggerUI::CPUStepEnded()
{
    if (m_StackTrace == nullptr)
    {
        return;
    }

    R4300iOpcode Opcode = g_System->Opcode();
    uint32_t op = Opcode.op;
    uint32_t funct = Opcode.funct;

    if (op == R4300i_JAL || ((op == R4300i_SPECIAL) && (funct == R4300i_SPECIAL_JALR) && (Opcode.rd == 31))) // JAL or JALR RA, x
    {
        m_StackTrace->PushEntry((uint32_t)g_System->JumpToLocation(), (uint32_t)g_Reg->m_PROGRAM_COUNTER);
    }
    else if (funct == R4300i_SPECIAL_JR && Opcode.rs == 31) // JR RA
    {
        m_StackTrace->PopEntry();
    }
    else if (op == R4300i_CP0 && funct == R4300i_COP0_CO_ERET) // TODO: may need more work
    {
        m_StackTrace->ClearEntries();
    }
}

void CDebuggerUI::FrameDrawn()
{
    // todo (upstream too): JS_HOOK_GFXUPDATE
}

void CDebuggerUI::PIFReadStarted(void)
{
    ScriptPifRead();
}

void CDebuggerUI::RSPReceivedTask(void)
{
    ScriptRspTask();
}

void CDebuggerUI::PIDMAReadStarted(void)
{
    ScriptPiDma(true);
}

void CDebuggerUI::PIDMAWriteStarted(void)
{
    ScriptPiDma(false);
    HandleCartToRamDMA();
}

void CDebuggerUI::EmulationStarted(void)
{
    m_EmulationClosing = false;
    ScriptEmuStateChange(EmuStarted);
}

void CDebuggerUI::EmulationStopped(void)
{
    ScriptEmuStateChange(EmuStopped);
    // the last call of the emulation thread to the script system
    m_EmulationClosing = false;
}

void CDebuggerUI::WaitForStep(void)
{
    g_Settings->SaveBool(Debugger_WaitingForStep, true);
    m_StepEvent.IsTriggered(SyncEvent::INFINITE_TIMEOUT);
    g_Settings->SaveBool(Debugger_WaitingForStep, false);
}

void CDebuggerUI::ReleaseStep()
{
    ReleaseScriptAlerts();
    if (g_Settings->LoadBool(Debugger_WaitingForStep) || g_DebugSettings.stepping)
    {
        g_Settings->SaveBool(Debugger_SteppingOps, false);
        m_StepEvent.Trigger();
    }
}

void CDebuggerUI::CopyCommands(uint32_t address, uint32_t count)
{
    if (m_CommandsView)
    {
        m_CommandsView->CopyCommands(address, count);
    }
}

bool CDebuggerUI::ExecutionBP(uint32_t address)
{
    return m_Breakpoints != nullptr && m_Breakpoints->ExecutionBPExists(address, true) != CBreakpoints::BP_NOT_SET;
}

bool CDebuggerUI::ReadBP8(uint32_t address)
{
    return m_Breakpoints != nullptr && m_Breakpoints->ReadBPExists8(address) != CBreakpoints::BP_NOT_SET;
}

bool CDebuggerUI::ReadBP16(uint32_t address)
{
    return m_Breakpoints != nullptr && m_Breakpoints->ReadBPExists16(address) != CBreakpoints::BP_NOT_SET;
}

bool CDebuggerUI::ReadBP32(uint32_t address)
{
    return m_Breakpoints != nullptr && m_Breakpoints->ReadBPExists32(address) != CBreakpoints::BP_NOT_SET;
}

bool CDebuggerUI::ReadBP64(uint32_t address)
{
    return m_Breakpoints != nullptr && m_Breakpoints->ReadBPExists64(address) != CBreakpoints::BP_NOT_SET;
}

bool CDebuggerUI::WriteBP8(uint32_t address)
{
    return m_Breakpoints != nullptr && m_Breakpoints->WriteBPExists8(address) != CBreakpoints::BP_NOT_SET;
}

bool CDebuggerUI::WriteBP16(uint32_t address)
{
    return m_Breakpoints != nullptr && m_Breakpoints->WriteBPExists16(address) != CBreakpoints::BP_NOT_SET;
}

bool CDebuggerUI::WriteBP32(uint32_t address)
{
    return m_Breakpoints != nullptr && m_Breakpoints->WriteBPExists32(address) != CBreakpoints::BP_NOT_SET;
}

bool CDebuggerUI::WriteBP64(uint32_t address)
{
    return m_Breakpoints != nullptr && m_Breakpoints->WriteBPExists64(address) != CBreakpoints::BP_NOT_SET;
}
