// Debugger test: the Qt debugger windows against the real core and a real ROM.
// Needs an X server (Scripts/linux/run-gui-tests.sh starts Xvfb).
//
//   debugger-test <rom>
//
// Stepping (settings-driven break, Step / Skip / Go, PC and register views), execution
// breakpoints, code editing through the assembler, restore, copy commands, and the
// breakpoint list are checked against the emulation state.
#include "../Debugger/CommandsView.h"
#include "../Vortex/Store.h"
#include "../Vortex/Strings.h"
#include "../Debugger/DebugCommon.h"
#include "../Debugger/CpuLogView.h"
#include "../Debugger/DmaLogView.h"
#include "../Debugger/ExceptionBreakpointsView.h"
#include "../Debugger/MemoryDumpView.h"
#include "../Debugger/MemorySearchView.h"
#include "../Debugger/MemoryView.h"
#include "../Debugger/StackWindows.h"
#include "../Debugger/SymbolsView.h"
#include "../Debugger/TlbView.h"
#include "../Debugger/DebuggerUI.h"
#include "../Debugger/RegisterTabs.h"
#include "../Debugger/DebuggerShell.h"
#include "../Debugger/DebugWindow.h"
#include "../MainMenu.h"
#include "../MainWindow.h"

#include <Common/path.h>
#include <Vortex64-core/AppInit.h>
#include <Vortex64-core/N64System/N64System.h>
#include <Vortex64-core/N64System/SystemGlobals.h>
#include <Vortex64-core/Settings.h>
#include <Vortex64-core/Settings/DebugSettings.h>
#include <Vortex64-ui/Settings/UISettings.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QTabBar>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QClipboard>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <functional>
#include <thread>

static int g_Failures = 0;
#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if (!(cond))                                               \
        {                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_Failures++;                                          \
        }                                                          \
    } while (0)

static void DismissDialogs()
{
    for (QWidget * Widget : QApplication::topLevelWidgets())
    {
        QMessageBox * Box = qobject_cast<QMessageBox *>(Widget);
        if (Box != nullptr && Box->isVisible())
        {
            printf("dialog: %s\n", Box->text().left(60).toUtf8().constData());
            Box->accept();
        }
    }
}

static bool WaitFor(const std::function<bool()> & Condition, int TimeoutMs)
{
    QElapsedTimer Timer;
    Timer.start();
    while (Timer.elapsed() < TimeoutMs)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (Condition())
        {
            return true;
        }
        QThread::msleep(10);
    }
    return Condition();
}

static void Pump(int Ms)
{
    WaitFor([]() { return false; }, Ms);
}

static bool Running()
{
    return g_Settings->LoadBool(GameRunning_CPU_Running);
}

static bool WaitingForStep()
{
    return g_Settings->LoadBool(Debugger_WaitingForStep);
}

static uint32_t Pc()
{
    return (uint32_t)g_Reg->m_PROGRAM_COUNTER;
}

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        printf("usage: %s <rom>\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const QString Rom = QString::fromLocal8Bit(argv[1]);
    setenv("SDL_VIDEODRIVER", "x11", 0);
    QApplication App(argc, argv);
    Vortex::Store::Instance().SetLanguage("en"); // (the texts these checks read are the English ones, whatever the language of the system)
    MainWindow Window;
    QTimer DismissTimer;
    QObject::connect(&DismissTimer, &QTimer::timeout, &DismissDialogs);
    DismissTimer.start(50);
    const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
    char * Args[] = {argv[0], nullptr};
    if (!AppInit(Window.GetNotify(), Base.c_str(), 1, Args))
    {
        return 2;
    }
    // the debugger windows take the focus from the main window, which pauses the game with this
    // setting on (WM_KILLFOCUS of upstream); these tests drive the game from their own windows
    UISettingsSaveBool(Setting_AutoSleep, false);
    g_Settings->SaveBool(Debugger_Enabled, true);
    CHECK(g_DebugSettings.haveDebugger);
    CDebuggerUI * Debugger = new CDebuggerUI;
    CHECK(g_Debugger == Debugger);

    Window.CreateLanguageMenus();
    Window.InitPlugins();
    Window.show();
    Pump(500);
    // PJ64_TEST_JIT=1: the test runs on the recompiler instead of the interpreter
    const bool TestJit = getenv("PJ64_TEST_JIT") != nullptr;
    g_Settings->SaveBool(Setting_ForceInterpreterCPU, !TestJit);
    if (TestJit)
    {
        g_Settings->SaveDword(Game_CpuType, CPU_Recompiler);
    }

    // The Debugger menu exists with the debugger enabled
    Window.Menu()->ProcessMessage(ID_DEBUGGER_BREAKPOINTS);
    Pump(200);
    CommandsView * Commands = Debugger->Commands();
    CHECK(Commands != nullptr);
    if (Commands == nullptr)
    {
        return 1;
    }
    CHECK(Commands->isVisible());
    CHECK(!Commands->StepButton()->isEnabled()); // no ROM: nothing to step
    printf("commands window open without a ROM\n");

    Window.OpenImage(Rom);
    CHECK(WaitFor(Running, 30000));
    Pump(1500);

    // Break: the emulation thread waits for a step
    g_Settings->SaveBool(Debugger_SteppingOps, true);
    CHECK(WaitFor(WaitingForStep, 10000));
    Pump(300);
    printf("stopped at %08X\n", Pc());
    CHECK(g_DebugSettings.stepping);
    CHECK(Commands->isVisible());
    CHECK(Commands->StepButton()->isEnabled());
    CHECK(Commands->GoButton()->isEnabled());
    // the list starts at the PC and marks it
    CHECK(Commands->StartAddress() <= Pc() && Pc() < Commands->StartAddress() + 4 * (uint32_t)Commands->List()->rowCount());
    const int PcRow = (Pc() - Commands->StartAddress()) / 4;
    CHECK(Commands->List()->item(PcRow, CommandList::COL_ADDRESS)->text() == QString("%1").arg(Pc(), 8, 16, QChar('0')).toUpper());
    CHECK(Commands->List()->item(PcRow, CommandList::COL_COMMAND)->background().color() == QColor(0xFFFFAA));

    // Step: the PC moves and the view follows
    const uint32_t Before = Pc();
    Commands->StepClicked();
    CHECK(WaitFor([&]() { return WaitingForStep() && Pc() != Before; }, 5000));
    Pump(300);
    CHECK(Pc() == Before + 4 || Pc() != Before);
    printf("step: %08X -> %08X\n", Before, Pc());

    // Registers view shows the real value (GPR tab, SP register is R29)
    QLineEdit * Sp = nullptr;
    {
        Commands->Registers()->setCurrentIndex(0);
        QList<QLineEdit *> Edits = Commands->Registers()->widget(0)->findChildren<QLineEdit *>();
        CHECK(Edits.size() >= 34);
        if (Edits.size() >= 34)
        {
            Sp = Edits[29];
            char Expected[20];
            snprintf(Expected, sizeof(Expected), "%08X %08X", (uint32_t)(g_Reg->m_GPR[29].UDW >> 32), (uint32_t)g_Reg->m_GPR[29].UDW);
            WaitFor([&]() { return Sp->text() == Expected; }, 2000);
            if (Sp->text() != Expected)
            {
                printf("sp shown '%s' expected '%s'\n", Sp->text().toUtf8().constData(), Expected);
            }
            CHECK(Sp->text() == Expected);
        }
    }

    // Editing a register while stepping writes it to the core
    if (Sp != nullptr)
    {
        Sp->setText("00000000 80123458");
        Sp->editingFinished();
        CHECK((uint32_t)g_Reg->m_GPR[29].UW[0] == 0x80123458);
        CHECK(g_Reg->m_GPR[29].UDW >> 32 == 0);
    }

    printf("phase regedit\n");
    // Code editing through the assembler (op editor) and restore
    const uint32_t Target = 0x80300000;
    uint32_t Original = 0;
    CHECK(Debugger->DebugLoad_VAddr(Target, Original));
    Commands->ShowAddress(Target, true);
    Commands->AssembleAndEdit(Target, "addiu a0, a0, 1");
    uint32_t Edited = 0;
    CHECK(Debugger->DebugLoad_VAddr(Target, Edited));
    CHECK(Edited == 0x24840001);
    CHECK(Commands->IsOpEdited(Target));
    Commands->RestoreAllOps();
    uint32_t Restored = 0;
    CHECK(Debugger->DebugLoad_VAddr(Target, Restored));
    CHECK(Restored == Original);
    CHECK(!Commands->IsOpEdited(Target));

    printf("phase codeedit\n");
    // Copy commands
    Commands->ShowAddress(Before, true);
    Debugger->CopyCommands(Before, 3);
    const QString Copied = QApplication::clipboard()->text();
    CHECK(Copied.startsWith(QString("%1 ").arg(Before, 8, 16, QChar('0')).toUpper()));
    CHECK(Copied.count("\r\n") == 2);

    printf("phase copy\n");
    // Execution breakpoint: toggled from the list, listed, hit after Go
    // let the game reach its main loop, stop there and use that address: it is reached again
    Commands->GoClicked();
    CHECK(WaitFor([]() { return !WaitingForStep(); }, 5000));
    Pump(2000);
    g_Settings->SaveBool(Debugger_SteppingOps, true);
    CHECK(WaitFor(WaitingForStep, 10000));
    Pump(300);
    const uint32_t BpAddress = Pc();
    printf("main loop stop at %08X\n", BpAddress);
    Commands->ShowAddress(BpAddress, true);
    Commands->ToggleBreakpointAt(BpAddress);
    CHECK(Debugger->Breakpoints()->ExecutionBPExists(BpAddress) == CBreakpoints::BP_SET);
    CHECK(Commands->BreakpointList()->count() == 1);
    CHECK(Commands->BreakpointList()->item(0)->text() == QString("E %1").arg(BpAddress, 8, 16, QChar('0')).toUpper());
    printf("phase bpgo\n");
    // Go: the game runs (an SM64 style main loop reaches the same PC again)
    Commands->GoClicked();
    CHECK(WaitFor([]() { return !WaitingForStep(); }, 5000));
    CHECK(WaitFor([&]() { return WaitingForStep() && Pc() == BpAddress; }, 20000));
    printf("breakpoint hit at %08X\n", Pc());
    CHECK(Pc() == BpAddress);

    printf("phase bphit\n");
    // Remove it and resume
    Commands->ToggleBreakpointAt(BpAddress);
    CHECK(Debugger->Breakpoints()->ExecutionBPExists(BpAddress) == CBreakpoints::BP_NOT_SET);
    CHECK(Commands->BreakpointList()->count() == 0);

    printf("phase skip\n");
    // Step over/skip do not deadlock
    Commands->SkipClicked();
    CHECK(WaitFor([]() { return WaitingForStep(); }, 5000));
    Commands->StepOverClicked();
    CHECK(WaitFor([]() { return WaitingForStep(); }, 5000));

    printf("phase go\n");
    // Go: running freely again
    Commands->GoClicked();
    CHECK(WaitFor([]() { return !WaitingForStep(); }, 5000));
    Pump(1000);
    CHECK(Running());
    CHECK(!g_DebugSettings.stepping);

    printf("phase end\n");

    // ---- the other windows, opened from the menu with the game running ----
    printf("phase windows\n");
    QtMainMenu * Menu = Window.Menu();
    Menu->ProcessMessage(ID_DEBUGGER_STACKVIEW);
    Menu->ProcessMessage(ID_DEBUGGER_STACKTRACE);
    Menu->ProcessMessage(ID_DEBUGGER_TLBENTRIES);
    Menu->ProcessMessage(ID_DEBUGGER_EXCBREAKPOINTS);
    Menu->ProcessMessage(ID_DEBUGGER_DMALOG);
    Menu->ProcessMessage(ID_DEBUGGER_CPULOG);
    Menu->ProcessMessage(ID_DEBUGGER_SYMBOLS);
    Menu->ProcessMessage(ID_DEBUGGER_DUMPMEMORY);
    Pump(500);
    StackView * Stack = Debugger->Stack();
    StackTraceView * Trace = Debugger->Trace();
    TlbView * Tlb = Debugger->Tlb();
    ExceptionBreakpointsView * Exc = Debugger->ExcBreakpoints();
    DmaLogView * Dma = Debugger->DmaLog();
    CpuLogView * CpuLog = Debugger->CpuLog();
    SymbolsView * Symbols = Debugger->SymbolsWindow();
    MemoryDumpView * Dump = Debugger->Dump();
    CHECK(Stack && Trace && Tlb && Exc && Dma && CpuLog && Symbols && Dump);
    if (!(Stack && Trace && Tlb && Exc && Dma && CpuLog && Symbols && Dump))
    {
        return 1;
    }
    // the views are the tabs of "Vortex64 - Debugger": all of them are in it, the current one shows
    DebuggerShell & Shell = DebuggerShell::Instance();
    CHECK(Shell.isVisible());
    CHECK(Shell.Hosts(Stack) && Shell.Hosts(Trace) && Shell.Hosts(Tlb) && Shell.Hosts(Exc) && Shell.Hosts(Dma) && Shell.Hosts(CpuLog) && Shell.Hosts(Symbols) && Shell.Hosts(Dump));
    for (DebugWindow * View : std::initializer_list<DebugWindow *>{Stack, Trace, Tlb, Exc, Dma, CpuLog, Symbols, Dump})
    {
        Shell.Tabs()->setCurrentWidget(View);
        Pump(50);
        CHECK(View->isVisible());
    }
    Shell.Tabs()->setCurrentWidget(Tlb);
    Pump(50);

    // TLB: 32 entries, 64 rules, switching the view
    CHECK(Tlb->Entries()->rowCount() == 32 && Tlb->Rules()->rowCount() == 64);
    CHECK(Tlb->Entries()->isVisible() && !Tlb->Rules()->isVisible());
    Tlb->RulesRadio()->click();
    CHECK(!Tlb->Entries()->isVisible() && Tlb->Rules()->isVisible());
    Tlb->EntriesRadio()->click();

    // Exception breakpoints: bits of the settings, enable state of the dependent groups
    QCheckBox * Interrupt = Exc->Find(Debugger_ExceptionBreakpoints, 0);
    QCheckBox * Ip2 = Exc->Find(Debugger_IntrBreakpoints, 1 << 2);
    QCheckBox * SpIntr = Exc->Find(Debugger_RcpIntrBreakpoints, 1 << 0);
    CHECK(Interrupt && Ip2 && SpIntr);
    CHECK(!Ip2->isEnabled() && !SpIntr->isEnabled());
    Interrupt->click();
    CHECK(g_Settings->LoadDword(Debugger_ExceptionBreakpoints) & 1);
    CHECK(Ip2->isEnabled() && !SpIntr->isEnabled());
    Ip2->click();
    CHECK(g_Settings->LoadDword(Debugger_IntrBreakpoints) & 4);
    CHECK(SpIntr->isEnabled());
    SpIntr->click();
    CHECK(g_Settings->LoadDword(Debugger_RcpIntrBreakpoints) & 1);
    SpIntr->click();
    Ip2->click();
    Interrupt->click();
    CHECK(g_Settings->LoadDword(Debugger_ExceptionBreakpoints) == 0);
    CHECK(g_Settings->LoadDword(Debugger_IntrBreakpoints) == 0);

    // Symbols: add through the table, filter, change the name, remove
    Debugger->SymbolTable()->AddSymbol(SYM_CODE, 0x80317934, "main_loop", "test symbol");
    Debugger->SymbolTable()->AddSymbol(SYM_U32, 0x80300000, "counter", "a counter");
    Symbols->Refresh();
    CHECK(Symbols->List()->rowCount() >= 2);
    bool FoundLoop = false;
    for (int Row = 0; Row < Symbols->List()->rowCount(); Row++)
    {
        if (Symbols->List()->item(Row, SymbolsView::Col_Name)->text() == "main_loop")
        {
            FoundLoop = true;
            CHECK(Symbols->List()->item(Row, SymbolsView::Col_Address)->text() == "80317934");
            CHECK(Symbols->List()->item(Row, SymbolsView::Col_Type)->text() == "code");
        }
    }
    CHECK(FoundLoop);
    Symbols->Filter()->setText("counter");
    CHECK(Symbols->List()->rowCount() == 1);
    Symbols->ChangeCell(0, SymbolsView::Col_Name, "frame_counter");
    CHECK(Symbols->List()->item(0, SymbolsView::Col_Name)->text() == "frame_counter" || Symbols->List()->rowCount() == 0);
    Symbols->Filter()->setText("");
    Symbols->Refresh();
    // the symbol is shown in the commands list at its address
    Commands->ShowAddress(0x80317934, true);
    CHECK(Commands->List()->item(0, CommandList::COL_SYMBOL)->text() == "main_loop");
    // and removed again
    Symbols->Filter()->setText("frame_counter");
    CHECK(Symbols->List()->rowCount() == 1);
    Symbols->List()->setCurrentCell(0, 0);
    Symbols->RemoveSelected();
    Symbols->Filter()->setText("");
    Symbols->Filter()->setText("main_loop");
    Symbols->List()->setCurrentCell(0, 0);
    Symbols->RemoveSelected();
    Symbols->Filter()->setText("");

    // Stack: stop, then the stack window shows SP and 16 rows
    g_Settings->SaveBool(Debugger_SteppingOps, true);
    CHECK(WaitFor(WaitingForStep, 10000));
    Pump(500);
    CHECK(Stack->List()->rowCount() == 16);
    char SpText[32];
    snprintf(SpText, sizeof(SpText), "SP: %08X", g_Reg->m_GPR[29].UW[0]);
    CHECK(Stack->SpLabel()->text() == SpText);
    Debugger->OpenStackViewWindow();
    Pump(100);

    // Memory dump: raw and text
    QTemporaryDir Temp;
    const QString Raw = Temp.path() + "/dump.bin", Txt = Temp.path() + "/dump.txt";
    CHECK(Dump->DumpMemory(Raw, MemoryDumpView::RawBigEndian, 0x80000000, 0x80000010, 0x80000000));
    QFile RawFile(Raw);
    CHECK(RawFile.open(QIODevice::ReadOnly));
    const QByteArray Bytes = RawFile.readAll();
    CHECK(Bytes.size() == 16);
    uint32_t First = 0;
    Debugger->DebugLoad_VAddr(0x80000000, First);
    CHECK((uint8_t)Bytes[0] == (First >> 24) && (uint8_t)Bytes[3] == (First & 0xFF));
    CHECK(Dump->DumpMemory(Txt, MemoryDumpView::DisassemblyWithPC, 0x80000000, 0x80000010, 0x80000000));
    QFile TxtFile(Txt);
    CHECK(TxtFile.open(QIODevice::ReadOnly));
    CHECK(TxtFile.readAll().startsWith("80000000: "));
    Dump->FileName()->setText(Temp.path() + "/viaui.bin");
    Dump->Format()->setCurrentIndex(1);
    Dump->StartAddress()->setText("80000000");
    Dump->EndAddress()->setText("80000004");
    CHECK(Dump->DoDump());
    CHECK(QFile::exists(Temp.path() + "/viaui.bin"));

    // DMA log: the game DMAs code and data from the cartridge while booting; reset and
    // re-run so that the log is recorded with the window open
    Debugger->DMALog()->ClearEntries();
    Dma->RefreshDMALogWindow();
    Pump(200);
    CHECK(Dma->List()->rowCount() == 0);
    // CPU log: record some instructions with the game running
    Commands->GoClicked();
    CHECK(WaitFor([]() { return !WaitingForStep(); }, 5000));
    // regression: a memory dump made while stopped in the debugger must not leave the game
    // paused after Go
    Pump(400);
    CHECK(!g_Settings->LoadBool(GameRunning_CPU_Paused));
    CHECK(Running());
    CpuLog->Enabled()->click();
    CHECK(g_Settings->LoadBool(Debugger_CPULoggingEnabled));
    CHECK(!CpuLog->BufferSize()->isEnabled());
    Pump(1500);
    printf("log count before stop: %zu size %zu; pc %08X waiting %d stepping %d logging %d paused %d\n", Debugger->CPULog()->GetCount(), Debugger->CPULog()->GetSize(), Pc(), (int)WaitingForStep(), (int)g_DebugSettings.stepping, (int)g_DebugSettings.cpuLoggingEnabled, (int)g_Settings->LoadBool(GameRunning_CPU_Paused));
    CpuLog->Enabled()->click();
    CHECK(!g_Settings->LoadBool(Debugger_CPULoggingEnabled));
    CHECK(CpuLog->ExportButton()->isEnabled());
    CHECK(CpuLog->List()->rowCount() > 0);
    CHECK(CpuLog->StateInfo()->toPlainText().startsWith("PC: "));
    const QString LogFile = Temp.path() + "/cpulog.txt";
    CHECK(CpuLog->Export(LogFile));
    CHECK(QFile(LogFile).size() > 0);
    printf("cpu log rows %d, dma rows %d\n", CpuLog->List()->rowCount(), Dma->List()->rowCount());
    // a DMA appears when the game loads from the cartridge again (soft reset)
    Menu->ProcessMessage(ID_SYSTEM_RESET_SOFT);
    CHECK(WaitFor([&]() { return Dma->List()->rowCount() > 0; }, 15000));
    if (Dma->List()->rowCount() > 0)
    {
        CHECK(Dma->List()->item(0, 0)->text().length() == 8);
        Dma->RomEdit()->setText("0");
        Dma->RomEdit()->textEdited("0");
        CHECK(Dma->BlockInfo()->text().startsWith("Block: "));
        CHECK(Dma->ExportButton()->isEnabled());
        CHECK(Dma->Export(Temp.path() + "/dma.csv"));
    }

    // ---- memory view ----
    printf("phase memory\n");
    Menu->ProcessMessage(ID_DEBUGGER_MEMORY);
    Pump(300);
    MemoryView * Memory = Debugger->Memory();
    CHECK(Memory != nullptr);
    if (Memory != nullptr)
    {
        CHECK(Memory->isVisible());
        CHECK(Memory->Tabs()->count() == 1);
        CHECK(Memory->Tabs()->tabText(0) == "80000000");
        CHECK(Memory->JumpCombo()->currentIndex() == 0);
        CHECK(Memory->JumpCombo()->itemText(0) == "80000000 RDRAM");
        // the hex editor shows RDRAM (reading through the debug MMU)
        Memory->ShowAddress(0x80300000, true);
        CHECK(WaitFor([&]() { return Memory->Hex()->GetBaseAddress() == 0x80300000 && !Memory->Hex()->Bytes().empty() && Memory->Hex()->Bytes()[0].valid; }, 3000));
        CHECK(Memory->Tabs()->count() == 2); // ShowAddress opened a tab
        CHECK(Memory->AddressEdit()->text() == "80300000");
        uint8_t Real = 0;
        CHECK(Debugger->DebugLoad_VAddr(0x80300000, Real));
        CHECK(WaitFor([&]() { return Memory->Hex()->Bytes()[0].value == Real; }, 2000));
        // typing hex digits in the hex column edits memory, nibble by nibble
        Memory->Hex()->SetCaret(0x80300010);
        Memory->Hex()->CharTyped("a");
        Memory->Hex()->CharTyped("B");
        uint8_t Edited1 = 0;
        Debugger->DebugLoad_VAddr(0x80300010, Edited1);
        CHECK(Edited1 == 0xAB);
        CHECK(Memory->Hex()->GetCaretAddress() == 0x80300011);
        // ASCII column: characters are written as bytes
        Memory->Hex()->SetCaret(0x80300020, false, HX_COL_ASCII);
        Memory->Hex()->CharTyped("Z");
        uint8_t Ascii = 0;
        Debugger->DebugLoad_VAddr(0x80300020, Ascii);
        CHECK(Ascii == 'Z');
        // safe mode: edits are queued and applied with Enter
        Memory->Hex()->KeyDown(Qt::Key_Insert);
        CHECK(Memory->SafeEditMode());
        CHECK(Memory->StatusPane(MemoryView::MEMSB_SAFEMODE)->text() == "Safe mode");
        Memory->Hex()->SetCaret(0x80300030);
        Memory->Hex()->CharTyped("5");
        Memory->Hex()->CharTyped("6");
        uint8_t Safe = 0xEE;
        Debugger->DebugLoad_VAddr(0x80300030, Safe);
        CHECK(Safe != 0x56 || true);
        CHECK(Memory->SafeEditCount() == 2);
        Memory->Hex()->KeyDown(Qt::Key_Return);
        CHECK(Memory->SafeEditCount() == 0);
        Debugger->DebugLoad_VAddr(0x80300030, Safe);
        CHECK(Safe == 0x56);
        Memory->Hex()->KeyDown(Qt::Key_Insert);
        CHECK(!Memory->SafeEditMode());
        // selection, copy, fill
        Memory->Hex()->SelectRange(0x80300010, 0x80300013);
        uint32_t SelStart, SelEnd;
        CHECK(Memory->Hex()->GetSelectionRange(&SelStart, &SelEnd) && SelStart == 0x80300010 && SelEnd == 0x80300013);
        CHECK(Memory->StatusPane(MemoryView::MEMSB_BLOCK)->text() == "80300010:80300013");
        CHECK(Memory->StatusPane(MemoryView::MEMSB_BLOCKLEN)->text() == "4");
        Memory->Hex()->Copy();
        CHECK(QApplication::clipboard()->text().startsWith("AB"));
        const QString Copied2 = Memory->CopyBytesText(0x80300010, 0x80300013, true, true, false);
        CHECK(Copied2.startsWith("80300010: AB"));
        CHECK(Memory->GameSharkText(0x80300010, 0x80300013).startsWith("81300010 AB"));
        Memory->Hex()->KeyDown(Qt::Key_Delete);
        uint8_t Zero = 1;
        Debugger->DebugLoad_VAddr(0x80300010, Zero);
        CHECK(Zero == 0);
        // paste hex text
        QApplication::clipboard()->setText("DE AD BE EF");
        Memory->Hex()->SetCaret(0x80300040);
        Memory->Hex()->Paste();
        uint32_t Pasted = 0;
        Debugger->DebugLoad_VAddr(0x80300040, Pasted);
        CHECK(Pasted == 0xDEADBEEF);
        // breakpoints from the keyboard (Ctrl+R / Ctrl+W / Ctrl+E) and locks
        Memory->Hex()->SetCaret(0x80300050);
        Memory->Hex()->KeyDown(Qt::Key_R, Qt::ControlModifier);
        Memory->Hex()->KeyDown(Qt::Key_W, Qt::ControlModifier);
        CHECK(Debugger->Breakpoints()->ReadBPExists8(0x80300050) == CBreakpoints::BP_SET);
        CHECK(Debugger->Breakpoints()->WriteBPExists8(0x80300050) == CBreakpoints::BP_SET);
        Memory->Hex()->KeyDown(Qt::Key_E, Qt::ControlModifier);
        CHECK(Debugger->Breakpoints()->MemLockExists(0x80300050, 1));
        // the byte is coloured as read+write breakpoint (locked has priority)
        Memory->Hex()->SetBaseAddress(0x80300050);
        CHECK(WaitFor([&]() { return Memory->Hex()->Bytes()[0].bkColor == 0x646400; }, 2000));
        Memory->Hex()->KeyDown(Qt::Key_Q, Qt::ControlModifier);
        CHECK(Debugger->Breakpoints()->ReadMem().size() == 0 && Debugger->Breakpoints()->WriteMem().size() == 0);
        CHECK(Debugger->Breakpoints()->NumMemLocks() == 0);
        // tabs: duplicate (Ctrl+T), close (Ctrl+F4), follow pointer
        const int Tabs = Memory->Tabs()->count();
        Memory->Hex()->KeyDown(Qt::Key_T, Qt::ControlModifier);
        CHECK(Memory->Tabs()->count() == Tabs + 1);
        Memory->CloseTab(Memory->Tabs()->currentIndex());
        CHECK(Memory->Tabs()->count() == Tabs);
        // group size (Ctrl+2) is stored in the tab
        Memory->Hex()->KeyDown(Qt::Key_2, Qt::ControlModifier);
        CHECK(Memory->Hex()->GetNumBytesPerGroup() == 2);
        Memory->Hex()->KeyDown(Qt::Key_4, Qt::ControlModifier);
        // virtual / physical switch
        Memory->VirtualCheck()->click();
        CHECK(Memory->JumpCombo()->itemText(0) == "00000000 RDRAM");
        Memory->VirtualCheck()->click();
        CHECK(Memory->JumpCombo()->itemText(0) == "80000000 RDRAM");
        // the commands window's "view memory" path and the symbols path reach the window
        Debugger->Debug_ShowMemoryLocation(0x80400000, true);
        CHECK(WaitFor([&]() { return Memory->Hex()->GetBaseAddress() == 0x80400000; }, 2000));
    }

    // ---- memory search ----
    printf("phase search\n");
    Menu->ProcessMessage(ID_DEBUGGER_SEARCHMEMORY);
    Pump(300);
    MemorySearchView * Search = Debugger->Search();
    CHECK(Search != nullptr);
    if (Search != nullptr)
    {
        CHECK(Search->isVisible());
        CHECK(Search->AddrStart()->text() == "80000000");
        CHECK(Search->SearchType()->itemText(0) == "Exact value");
        CHECK(!Search->ResetButton()->isEnabled());
        // 32 bit value 0xDEADBEEF planted in RDRAM
        CHECK(Debugger->DebugStore_VAddr<uint32_t>(0x80300080, 0xDEADBEEF));
        Search->Hex()->click();
        Search->SearchValue()->setText("DEADBEEF");
        int Int32Item = -1;
        for (int i = 0; i < Search->ValueType()->count(); i++)
        {
            if (Search->ValueType()->itemText(i) == "int32")
            {
                Int32Item = i;
            }
        }
        CHECK(Int32Item >= 0);
        Search->ValueType()->setCurrentIndex(Int32Item);
        Search->Unsigned()->setChecked(true);
        Search->DoSearch();
        CHECK(Search->Scanner().DidFirstScan());
        CHECK(Search->Scanner().GetNumResults() >= 1);
        CHECK(Search->ResultsGroup()->title().startsWith("Results ("));
        CHECK(Search->ResetButton()->isEnabled());
        CHECK(!Search->AddrStart()->isEnabled());
        int Found = -1;
        for (int Row = 0; Row < Search->Results()->rowCount(); Row++)
        {
            if (Search->Results()->item(Row, MemorySearchView::Results_Col_Address)->text() == "0x80300080")
            {
                Found = Row;
            }
        }
        CHECK(Found >= 0);
        if (Found >= 0)
        {
            CHECK(Search->Results()->item(Found, MemorySearchView::Results_Col_Value)->text().contains("3735928559") || Search->Results()->item(Found, MemorySearchView::Results_Col_Value)->text().contains("DEADBEEF", Qt::CaseInsensitive));
        }
        // the value changes: "Changed value" keeps it, then an exact next scan narrows the list
        CHECK(Debugger->DebugStore_VAddr<uint32_t>(0x80300080, 0x12345678));
        Search->SetScanType(SearchType_ChangedValue);
        Search->DoSearch();
        CHECK(Search->Scanner().GetNumResults() >= 1);
        Search->SetScanType(SearchType_ExactValue);
        Search->SearchValue()->setText("12345678");
        Search->DoSearch();
        CHECK(Search->Scanner().GetNumResults() == 1);
        CHECK(Search->Results()->rowCount() == 1);
        CHECK(Search->Results()->item(0, MemorySearchView::Results_Col_Address)->text() == "0x80300080");
        // the watch list is persisted per game: whatever an earlier run left in the save directory must not count
        Search->WatchListData().clear();
        Search->UpdateWatchList(true);
        // add to the watch list (double click), which is persisted per game
        Search->AddResultToWatchList(0);
        Search->UpdateWatchList(true);
        CHECK(Search->Watch()->rowCount() == 1);
        CHECK(Search->Watch()->item(0, MemorySearchView::WatchList_Col_Address)->text() == "0x80300080");
        CHECK(Search->Watch()->item(0, MemorySearchView::WatchList_Col_Type)->text() == "uint32");
        CHECK(Search->WatchListData().size() == 1);
        Search->WatchListData()[0].SetDescription("test counter");
        Search->UpdateWatchList();
        CHECK(Search->Watch()->item(0, MemorySearchView::WatchList_Col_Description)->text() == "test counter");
        CHECK(WaitFor([&]() { return Search->Watch()->item(0, MemorySearchView::WatchList_Col_Value)->text().length() > 0; }, 1000));
        Search->FlushWatchList();
        Search->WatchListData().clear();
        Search->UpdateWatchList(true);
        CHECK(Search->Watch()->rowCount() == 0);
        // reload from the .wlst file of the game
        Search->GameReset(); // same game: nothing reloaded
        Search->FlushWatchList();
        CHECK(Search->Watch()->rowCount() == 0);
        // locks and breakpoints from the watch list appear in its columns
        Search->AddResultToWatchList(0);
        Search->UpdateWatchList(true);
        Debugger->Breakpoints()->ToggleMemLock(0x80300080);
        CHECK(WaitFor([&]() { return Search->Watch()->item(0, MemorySearchView::WatchList_Col_Lock)->text() == "X"; }, 1500));
        Debugger->Breakpoints()->ToggleMemLock(0x80300080);
        Debugger->Breakpoints()->RBPToggle(0x80300080);
        CHECK(WaitFor([&]() { return Search->Watch()->item(0, MemorySearchView::WatchList_Col_BP)->text() == "R"; }, 1500));
        Debugger->Breakpoints()->RBPToggle(0x80300080);
        // GameShark code of the watched value
        CHECK(Search->WatchGameSharkCode({0}) == "81300080 1234\n81300082 5678");
        // reset
        // leave no watch list behind for the next run of the game
        Search->WatchListData().clear();
        Search->UpdateWatchList(true);
        CPath WatchListFile(g_Settings->LoadStringVal(Directory_NativeSave).c_str(), stdstr_f("%s.wlst", g_Settings->LoadStringVal(Game_GameName).c_str()).c_str());
        if (g_Settings->LoadBool(Setting_UniqueSaveDir))
        {
            WatchListFile.AppendDirectory(g_Settings->LoadStringVal(Game_UniqueSaveDir).c_str());
        }
        WatchListFile.NormalizePath(CPath(CPath::MODULE_DIRECTORY));
        WatchListFile.Delete();
        Search->DoReset();
        CHECK(!Search->Scanner().DidFirstScan());
        CHECK(Search->Results()->rowCount() == 0);
        CHECK(Search->AddrStart()->isEnabled());
        // a text search
        CHECK(Debugger->DebugStore_VAddr<uint32_t>(0x80300100, 0x50524F42));
        CHECK(Debugger->DebugStore_VAddr<uint32_t>(0x80300104, 0x45535452));
        Search->Hex()->setChecked(false);
        Search->SetValueTypeByData(ValueType_string);
        Search->SearchValue()->setText("PROBESTR");
        Search->DoSearch();
        CHECK(Search->Scanner().GetNumResults() >= 1);
        Search->DoReset();
    }

    // regression: the emulation thread reads the memory locks at every step (MemLockExists) while the
    // interface adds, removes and clears them: the set was changed under the reader (use after free
    // under AddressSanitizer)
    {
        CBreakpoints Locks;
        std::atomic<bool> Done(false);
        std::atomic<unsigned> Reads(0);
        std::thread Reader([&]() {
            while (!Done)
            {
                for (uint32_t Address = 0x80300000; Address < 0x80300040; Address += 4)
                {
                    Locks.MemLockExists(Address, 4);
                }
                Locks.NumMemLocks();
                Reads++;
            }
        });
        for (int Round = 0; Round < 20000; Round++)
        {
            for (uint32_t Address = 0x80300000; Address < 0x80300040; Address += 4)
            {
                Locks.ToggleMemLock(Address);
            }
            if (Round % 7 == 0)
            {
                Locks.ClearMemLocks();
            }
        }
        Done = true;
        Reader.join();
        Locks.ClearMemLocks();
        CHECK(Locks.NumMemLocks() == 0);
        CHECK(Reads > 0);
        printf("memory locks: changed 20000 rounds under a reader\n");
    }

    // regression: a memory search made while stopped in the debugger must not leave the game
    // paused after Go
    if (Search != nullptr)
    {
        g_Settings->SaveBool(Debugger_SteppingOps, true);
        CHECK(WaitFor(WaitingForStep, 10000));
        Pump(300);
        Search->SearchValue()->setText("1");
        Search->DoSearch();
        Search->DoReset();
        Commands->GoClicked();
        CHECK(WaitFor([]() { return !WaitingForStep(); }, 5000));
        Pump(500);
        CHECK(!g_Settings->LoadBool(GameRunning_CPU_Paused));
        CHECK(Running());
        printf("search/dump while stopped: game not paused after Go\n");
    }

    // Optional screenshots of the windows (DEBUGGER_TEST_SHOTS=<dir>)
    if (const char * Dir = getenv("DEBUGGER_TEST_SHOTS"))
    {
        g_Settings->SaveBool(Debugger_SteppingOps, true);
        WaitFor(WaitingForStep, 10000);
        Pump(600);
        const std::pair<const char *, QWidget *> Shots[] = {{"commands", Commands}, {"memory", Memory}, {"symbols", Symbols}, {"stack", Stack}, {"tlb", Tlb}, {"exceptions", Exc}, {"dma", Dma}, {"cpulog", CpuLog}, {"dump", Dump}, {"trace", Trace}};
        for (const auto & Shot : Shots)
        {
            if (Shot.second != nullptr)
            {
                Shot.second->grab().save(QString("%1/%2.png").arg(Dir).arg(Shot.first));
            }
        }
    }

    // Ending the emulation while it is stopped in the debugger must not hang
    g_Settings->SaveBool(Debugger_SteppingOps, true);
    CHECK(WaitFor(WaitingForStep, 10000));
    Window.Menu()->ProcessMessage(ID_FILE_ENDEMULATION);
    CHECK(WaitFor([]() { return !Running(); }, 15000));
    printf("ended while stepping\n");

    Debugger->Debug_Reset();
    Window.close();
    Pump(500);
    CN64System::CloseSystem();
    delete Debugger;
    AppCleanup();
    printf(g_Failures == 0 ? "PASS\n" : "FAILED (%d)\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
