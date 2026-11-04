#include "stdafx.h"
#include "../Vortex/Strings.h"
#include "CommandsView.h"
#include "DebugCommon.h"
#include "DebugDialogs.h"
#include "DebuggerUI.h"
#include "RegisterTabs.h"

#include <Common/SyncEvent.h>
#include <Project64-core/N64System/Mips/R4300iInstruction.h>
#include <Project64-core/N64System/N64Rom.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Settings/DebugSettings.h>
#include <Project64/UserInterface/Debugger/Assembler.h>

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPointer>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QWheelEvent>

#include "AddressNotes.inc"
#include <algorithm>

using Vortex::Tr;

// ---------------------------------------------------------------------------------------
// CommandList
// ---------------------------------------------------------------------------------------

CommandList::CommandList(QWidget * Parent) :
    QTableWidget(0, 5, Parent)
{
    setHorizontalHeaderLabels({"", Tr("Address"), Tr("Command"), Tr("Parameters"), Tr("Symbol")});
    verticalHeader()->hide();
    verticalHeader()->setDefaultSectionSize(RowHeight());
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    horizontalHeader()->setSectionsClickable(false);
    horizontalHeader()->setStretchLastSection(true);
    // widths of the font of the moment (HiDPI, other fonts): an address of 8 digits always shows whole
    const int Char = fontMetrics().averageCharWidth();
    setColumnWidth(COL_ARROWS, std::max(30, 4 * Char));
    setColumnWidth(COL_ADDRESS, fontMetrics().horizontalAdvance(QStringLiteral("00000000")) + 3 * Char);
    setColumnWidth(COL_COMMAND, std::max(90, 13 * Char));
    setColumnWidth(COL_PARAMETERS, std::max(130, 20 * Char));
    setColumnWidth(COL_SYMBOL, std::max(180, 26 * Char));
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setShowGrid(false);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
}

int CommandList::RowHeight() const
{
    return fontMetrics().height() + 2;
}

// Draw branch arrows (DrawBranchArrows)
void CommandList::paintEvent(QPaintEvent * Event)
{
    QTableWidget::paintEvent(Event);

    static const QColor Colors[] = {
        QColor(240, 240, 240), QColor(30, 135, 255), QColor(255, 0, 200), QColor(215, 155, 0), QColor(100, 180, 0),
        QColor(200, 100, 255), QColor(120, 120, 120), QColor(0, 220, 160), QColor(255, 100, 0), QColor(255, 255, 0)};
    const int NumColors = sizeof(Colors) / sizeof(Colors[0]);

    QPainter Painter(viewport());
    const int ColWidth = columnWidth(COL_ARROWS);
    const int Bottom = viewport()->height();
    const QColor Background(30, 30, 30);
    Painter.fillRect(QRect(0, 0, ColWidth, Bottom), Background);

    const int BaseX = ColWidth - 4;
    const int BaseY = RowHeight() / 2;
    const int Row = RowHeight();
    for (size_t i = 0; i < Arrows.size(); i++)
    {
        const QColor Color = Colors[i % NumColors];
        const BranchArrow & Arrow = Arrows[i];
        const int BegX = BaseX - Arrow.startMargin * 3;
        const int EndX = BaseX - Arrow.endMargin * 3;
        const int BegY = BaseY + Arrow.startPos * Row;
        int EndY = BaseY + Arrow.endPos * Row;
        bool EndVisible = true;
        if (EndY < 0)
        {
            EndY = 1;
            EndVisible = false;
        }
        else if (EndY > Bottom)
        {
            EndY = Bottom - 2;
            EndVisible = false;
        }
        const int MarginX = BaseX - (4 + Arrow.margin * 3);

        // Start pointer
        Painter.setPen(Color);
        Painter.drawPoint(BegX + 0, BegY - 1);
        Painter.drawPoint(BegX + 1, BegY - 2);
        Painter.drawPoint(BegX + 0, BegY + 1);
        Painter.drawPoint(BegX + 1, BegY + 2);

        QPolygon Path;
        Path << QPoint(BegX - 1, BegY) << QPoint(MarginX, BegY) << QPoint(MarginX, EndY);
        if (EndVisible)
        {
            Path << QPoint(EndX + 2, EndY);
        }
        Painter.setPen(QPen(Background, 3));
        Painter.drawPolyline(Path);
        Painter.setPen(QPen(Color, 1));
        Painter.drawPolyline(Path);

        if (EndVisible)
        {
            Painter.setPen(Color);
            Painter.drawPoint(EndX - 0, EndY - 1);
            Painter.drawPoint(EndX - 1, EndY - 2);
            Painter.drawPoint(EndX - 1, EndY - 1);
            Painter.drawPoint(EndX - 0, EndY + 1);
            Painter.drawPoint(EndX - 1, EndY + 2);
            Painter.drawPoint(EndX - 1, EndY + 1);
            Painter.setPen(Background);
            Painter.drawPoint(EndX - 1, EndY + 3);
            Painter.drawPoint(EndX - 1, EndY - 3);
        }
    }
}

// ---------------------------------------------------------------------------------------
// CommandsView
// ---------------------------------------------------------------------------------------

CommandsView::CommandsView(CDebuggerUI * Debugger, SyncEvent & StepEvent) :
    DebugWindow(Debugger, DebuggerUI_CommandsPos, true),
    m_Breakpoints(Debugger->Breakpoints()),
    m_StepEvent(StepEvent)
{
    setWindowTitle(Tr("Commands"));
    resize(535 * 1.5, 327 * 1.5);
    m_StartAddress = (uint32_t)(g_Reg ? g_Reg->m_PROGRAM_COUNTER : 0x80000000);
    BuildUi();

    g_Settings->RegisterChangeCB(GameRunning_CPU_Running, this, (CSettings::SettingChangedFunc)StaticGameCpuRunningChanged);
    g_Settings->RegisterChangeCB(Debugger_WaitingForStep, this, (CSettings::SettingChangedFunc)StaticWaitingForStepChanged);
    g_Settings->RegisterChangeCB(Debugger_SteppingOps, this, (CSettings::SettingChangedFunc)StaticSteppingOpsChanged);

    ShowAddress(m_StartAddress, true);
    if (g_DebugSettings.stepping)
    {
        EnableStepButtons(true);
    }
    LoadWindowPos();
    m_Attached = true;
    RecompilerCheck();
}

CommandsView::~CommandsView()
{
    m_Attached = false;
    g_Settings->UnregisterChangeCB(GameRunning_CPU_Running, this, (CSettings::SettingChangedFunc)StaticGameCpuRunningChanged);
    g_Settings->UnregisterChangeCB(Debugger_WaitingForStep, this, (CSettings::SettingChangedFunc)StaticWaitingForStepChanged);
    g_Settings->UnregisterChangeCB(Debugger_SteppingOps, this, (CSettings::SettingChangedFunc)StaticSteppingOpsChanged);
}

void CommandsView::BuildUi()
{
    QGridLayout * Grid = new QGridLayout(this);
    Grid->setContentsMargins(0, 0, 4, 4);
    Grid->setSpacing(2);

    m_CommandList = new CommandList(this);
    m_CommandList->viewport()->installEventFilter(this);
    m_CommandList->setContextMenuPolicy(Qt::CustomContextMenu);
    Grid->addWidget(m_CommandList, 0, 0, 3, 1);

    m_Scrollbar = new QScrollBar(Qt::Vertical, this);
    m_Scrollbar->setRange(0, 100);
    m_Scrollbar->setValue(50);
    Grid->addWidget(m_Scrollbar, 0, 1, 3, 1);
    connect(m_Scrollbar, &QScrollBar::actionTriggered, this, [this](int Action) {
        // WM_VSCROLL: SB_LINEUP/SB_LINEDOWN move by one instruction
        if (Action == QAbstractSlider::SliderSingleStepSub)
        {
            ShowAddress(m_StartAddress - 4, true);
        }
        else if (Action == QAbstractSlider::SliderSingleStepAdd)
        {
            ShowAddress(m_StartAddress + 4, true);
        }
        m_Scrollbar->setValue(50);
    });

    QWidget * Side = new QWidget(this);
    QGridLayout * SideGrid = new QGridLayout(Side);
    SideGrid->setContentsMargins(0, 0, 0, 0);
    SideGrid->setSpacing(2);
    Grid->addWidget(Side, 0, 2, 3, 1);
    Grid->setColumnStretch(0, 1);

    // Row 0: history, address, symbols, skip/step/step over/go
    QHBoxLayout * Top = new QHBoxLayout;
    Top->setSpacing(1);
    m_BackButton = new QPushButton(QString::fromUtf8("\xe2\x97\x80"), Side);
    m_BackButton->setProperty("vx", "tool");
    m_ForwardButton = new QPushButton(QString::fromUtf8("\xe2\x96\xb6"), Side);
    m_ForwardButton->setProperty("vx", "tool");
    m_BackButton->setFixedWidth(24);
    m_ForwardButton->setFixedWidth(24);
    m_AddressEdit = new NumberEdit(Side);
    m_AddressEdit->setFixedWidth(80);
    QPushButton * SymbolsButton = new QPushButton("...", Side);
    SymbolsButton->setProperty("vx", "tool");
    SymbolsButton->setFixedWidth(30);
    SymbolsButton->setToolTip(Tr("Symbols..."));
    m_SkipButton = new QPushButton(Tr("Skip"), Side);
    m_StepButton = new QPushButton(Tr("Step"), Side);
    m_StepOverButton = new QPushButton(Tr("St.Ovr"), Side);
    m_GoButton = new QPushButton(Tr("Go"), Side);
    m_SkipButton->setToolTip(Tr("Skip (F1)"));
    m_StepButton->setToolTip(Tr("Step (F2)"));
    m_StepOverButton->setToolTip(Tr("Step Over (F3)"));
    m_GoButton->setToolTip(Tr("Go (F4)"));
    for (QWidget * Widget : std::initializer_list<QWidget *>{m_BackButton, m_ForwardButton, m_AddressEdit, SymbolsButton, m_SkipButton, m_StepButton, m_StepOverButton, m_GoButton})
    {
        Top->addWidget(Widget);
    }
    SideGrid->addLayout(Top, 0, 0, 1, 3);

    // Row 1: PC + breakpoints
    QHBoxLayout * PcRow = new QHBoxLayout;
    PcRow->addWidget(new QLabel("PC", Side));
    m_PCEdit = new NumberEdit(Side);
    m_PCEdit->setFixedWidth(80);
    m_PCEdit->setAlignment(Qt::AlignRight);
    m_ViewPCButton = new QPushButton(Tr("View"), Side);
    m_ViewPCButton->setToolTip(Tr("Jump to program counter"));
    PcRow->addWidget(m_PCEdit);
    PcRow->addWidget(m_ViewPCButton);
    PcRow->addStretch();
    SideGrid->addLayout(PcRow, 1, 0);

    m_BreakpointList = new QListWidget(Side);
    m_BreakpointList->setToolTip(Tr("Active breakpoints"));
    m_BreakpointList->setMaximumHeight(80);
    SideGrid->addWidget(m_BreakpointList, 1, 1, 1, 1);
    QVBoxLayout * BpButtons = new QVBoxLayout;
    BpButtons->setSpacing(0);
    QPushButton * AddBp = new QPushButton("+", Side);
    AddBp->setProperty("vx", "tool");
    QPushButton * RemoveBp = new QPushButton("-", Side);
    RemoveBp->setProperty("vx", "tool");
    QPushButton * ClearBp = new QPushButton("x", Side);
    ClearBp->setProperty("vx", "tool");
    AddBp->setToolTip(Tr("Add breakpoint..."));
    RemoveBp->setToolTip(Tr("Remove selected breakpoint"));
    ClearBp->setToolTip(Tr("Remove all breakpoints"));
    for (QPushButton * Button : {AddBp, RemoveBp, ClearBp})
    {
        Button->setFixedSize(24, 20);
        BpButtons->addWidget(Button);
    }
    SideGrid->addLayout(BpButtons, 1, 2);

    m_RegisterTabs = new RegisterTabs(m_Debugger, Side);
    SideGrid->addWidget(m_RegisterTabs, 2, 0, 1, 3);
    SideGrid->setRowStretch(2, 1);

    QHBoxLayout * CopyRow = new QHBoxLayout;
    QPushButton * CopyTab = new QPushButton(Tr("Copy Tab Registers"), Side);
    QPushButton * CopyAll = new QPushButton(Tr("Copy All Registers"), Side);
    CopyRow->addWidget(CopyTab);
    CopyRow->addWidget(CopyAll);
    CopyRow->addStretch();
    SideGrid->addLayout(CopyRow, 3, 0, 1, 3);

    m_OpEdit = new QLineEdit(m_CommandList->viewport());
    m_OpEdit->hide();
    m_OpEdit->installEventFilter(this);
    m_OpEdit->setFont(m_CommandList->font());
    m_OpEdit->setFrame(false);

    EnableStepButtons(false);
    m_ViewPCButton->setEnabled(false);
    ToggleHistoryButtons();
    RefreshBreakpointList();
    m_PCEdit->SetValue(m_StartAddress);
    m_AddressEdit->SetValue(m_StartAddress);

    connect(m_AddressEdit, &NumberEdit::ValueEdited, this, [this]() {
        if (m_Attached)
        {
            ShowAddress(m_AddressEdit->Value(), true, true);
        }
    });
    connect(m_PCEdit, &NumberEdit::ValueEdited, this, [this]() {
        if (m_Attached && g_Reg != nullptr && g_DebugSettings.stepping)
        {
            g_Reg->m_PROGRAM_COUNTER = m_PCEdit->Value();
        }
    });
    connect(m_BackButton, &QPushButton::clicked, this, [this]() {
        if (m_HistoryIndex > 0)
        {
            m_HistoryIndex--;
            GoToAddress(m_History[m_HistoryIndex]);
            ToggleHistoryButtons();
        }
    });
    connect(m_ForwardButton, &QPushButton::clicked, this, [this]() {
        if (m_History.size() > 0 && m_HistoryIndex < (int)m_History.size() - 1)
        {
            m_HistoryIndex++;
            GoToAddress(m_History[m_HistoryIndex]);
            ToggleHistoryButtons();
        }
    });
    connect(m_ViewPCButton, &QPushButton::clicked, this, [this]() {
        if (g_Reg != nullptr && g_DebugSettings.stepping)
        {
            ShowAddress((uint32_t)g_Reg->m_PROGRAM_COUNTER, true);
        }
    });
    connect(SymbolsButton, &QPushButton::clicked, this, [this]() { m_Debugger->OpenSymbolsWindow(); });
    connect(m_GoButton, &QPushButton::clicked, this, [this]() {
        CPUResume();
        m_AddressEdit->setFocus();
    });
    connect(m_StepButton, &QPushButton::clicked, this, [this]() { StepClicked(); });
    connect(m_StepOverButton, &QPushButton::clicked, this, [this]() { CPUStepOver(); });
    connect(m_SkipButton, &QPushButton::clicked, this, [this]() {
        CPUSkip();
        m_AddressEdit->setFocus();
    });
    connect(ClearBp, &QPushButton::clicked, this, [this]() {
        m_Breakpoints->BPClear();
        RefreshBreakpointList();
        ShowAddress(m_StartAddress, true);
    });
    connect(AddBp, &QPushButton::clicked, this, [this]() {
        DebugDialogs::AddBreakpoint(m_Debugger, this);
        RefreshBreakpointList();
        ShowAddress(m_StartAddress, true);
    });
    connect(RemoveBp, &QPushButton::clicked, this, [this]() {
        RemoveSelectedBreakpoints();
        ShowAddress(m_StartAddress, true);
    });
    connect(CopyTab, &QPushButton::clicked, this, [this]() { m_RegisterTabs->CopyTabRegisters(); });
    connect(CopyAll, &QPushButton::clicked, this, [this]() { m_RegisterTabs->CopyAllRegisters(); });
    connect(m_BreakpointList, &QListWidget::itemClicked, this, [this](QListWidgetItem * Item) {
        // OnListBoxClicked: execution breakpoints show the code, the others the memory
        const uint32_t Address = Item->data(Qt::UserRole).toUInt();
        if (Item->text().startsWith('E'))
        {
            ShowAddress(Address, true);
        }
        else
        {
            m_Debugger->Debug_ShowMemoryLocation(Address, true);
        }
    });
    connect(m_CommandList, &QTableWidget::cellClicked, this, [this](int, int) { EndOpEdit(); });
    connect(m_CommandList, &QTableWidget::cellDoubleClicked, this, [this](int Row, int) {
        // Set PC breakpoint
        ToggleBreakpointAt(m_StartAddress + Row * 4);
        m_AddressEdit->setFocus();
    });
    connect(m_CommandList, &QWidget::customContextMenuRequested, this, [this](const QPoint & Pos) {
        const QModelIndex Index = m_CommandList->indexAt(m_CommandList->viewport()->mapFrom(m_CommandList, Pos));
        const int Row = m_CommandList->rowAt(m_CommandList->viewport()->mapFrom(m_CommandList, Pos).y());
        (void)Index;
        if (Row >= 0)
        {
            ShowContextMenu(Row, m_CommandList->mapToGlobal(Pos));
        }
    });
    connect(m_RegisterTabs, &QTabWidget::currentChanged, this, [this](int) { m_RegisterTabs->RefreshEdits(); });

    // Keyboard hooks (InterceptKeyDown): F1 skip, F2 step, F3 step over, F4 go
    auto Key = [this](Qt::Key K, std::function<void()> F) {
        QShortcut * Shortcut = new QShortcut(QKeySequence(K), this);
        Shortcut->setContext(Qt::WindowShortcut);
        connect(Shortcut, &QShortcut::activated, this, F);
    };
    Key(Qt::Key_F1, [this]() { CPUSkip(); });
    Key(Qt::Key_F2, [this]() {
        if (g_DebugSettings.waitingForStep)
        {
            m_StepEvent.Trigger();
        }
    });
    Key(Qt::Key_F3, [this]() { CPUStepOver(); });
    Key(Qt::Key_F4, [this]() { CPUResume(); });
}

void CommandsView::GoToAddress(uint32_t Address)
{
    m_AddressEdit->SetValue(Address);
    ShowAddress(Address, true, true);
}

void CommandsView::EnableStepButtons(bool Enable)
{
    m_ViewPCButton->setEnabled(Enable);
    m_StepButton->setEnabled(Enable);
    m_StepOverButton->setEnabled(Enable);
    m_SkipButton->setEnabled(Enable);
    m_GoButton->setEnabled(Enable);
}

void CommandsView::StaticSteppingOpsChanged(CommandsView * This)
{
    QPointer<CommandsView> Guard(This);
    RunOnGuiThread([Guard]() {
        if (Guard)
        {
            Guard->SteppingOpsChanged();
        }
    });
}

void CommandsView::StaticWaitingForStepChanged(CommandsView * This)
{
    QPointer<CommandsView> Guard(This);
    RunOnGuiThread([Guard]() {
        if (Guard)
        {
            Guard->WaitingForStepChanged();
        }
    });
}

void CommandsView::StaticGameCpuRunningChanged(CommandsView * This)
{
    QPointer<CommandsView> Guard(This);
    RunOnGuiThread([Guard]() {
        if (Guard)
        {
            Guard->RecompilerCheck();
        }
    });
}

void CommandsView::RecompilerCheck()
{
    if (!g_Settings->LoadBool(GameRunning_CPU_Running))
    {
        return;
    }
    if (g_DebugSettings.haveDebugger && !g_Settings->LoadBool(Setting_ForceInterpreterCPU) && (CPU_TYPE)g_Settings->LoadDword(Game_CpuType) != CPU_Interpreter)
    {
        QMessageBox::warning(this, Tr("Warning"),
                             Tr("Debugger support for the recompiler core is experimental.\n\n"
                                "For optimal experience, enable \"Always use interpreter core\" "
                                "in advanced settings and restart the emulator."));
    }
}

void CommandsView::showEvent(QShowEvent * Event)
{
    DebugWindow::showEvent(Event);
    RedrawCommands();
}

void CommandsView::resizeEvent(QResizeEvent * Event)
{
    DebugWindow::resizeEvent(Event);
    RedrawCommands();
}

// OnActivate: refresh when the window becomes active
void CommandsView::changeEvent(QEvent * Event)
{
    DebugWindow::changeEvent(Event);
    if (Event->type() == QEvent::ActivationChange && isActiveWindow() && m_Attached)
    {
        ShowAddress(m_StartAddress, true);
        RefreshBreakpointList();
    }
}

// RedrawCommandsAndRegisters: the number of rows follows the size of the list
void CommandsView::RedrawCommands()
{
    if (m_CommandList == nullptr)
    {
        return;
    }
    const int Rows = m_CommandList->viewport()->height() / m_CommandList->RowHeight();
    if (Rows > 0 && m_CommandListRows != Rows)
    {
        m_CommandListRows = Rows;
        ShowAddress(m_StartAddress, true);
    }
}

// InterceptMouseWheel
void CommandsView::wheelEvent(QWheelEvent * Event)
{
    const int Notches = Event->angleDelta().y() / 120;
    if (Notches != 0)
    {
        m_StartAddress -= Notches * 4;
        m_AddressEdit->SetValue(m_StartAddress);
        ShowAddress(m_StartAddress, true, true);
    }
    Event->accept();
}

bool CommandsView::eventFilter(QObject * Watched, QEvent * Event)
{
    if (Watched == m_CommandList->viewport() && Event->type() == QEvent::Wheel)
    {
        wheelEvent(static_cast<QWheelEvent *>(Event));
        return true;
    }
    if (Watched == m_OpEdit && Event->type() == QEvent::KeyPress)
    {
        QKeyEvent * Key = static_cast<QKeyEvent *>(Event);
        if (Key->matches(QKeySequence::Paste))
        {
            const QString Text = QApplication::clipboard()->text();
            if (Text.contains('\n') || Text.contains('\r'))
            {
                OpEditMultiline(Text);
                return true;
            }
            return false;
        }
        return HandleOpEditKey(Key->key());
    }
    return DebugWindow::eventFilter(Watched, Event);
}

void CommandsView::AddBranchArrow(int StartPos, int EndPos)
{
    int StartMargin = 0, EndMargin = 0, Margin = 0;
    for (const CommandList::BranchArrow & Arrow : m_CommandList->Arrows)
    {
        // Arrow's start or end position within another arrow's stride
        if ((StartPos >= Arrow.startPos && StartPos <= Arrow.endPos) || (EndPos >= Arrow.startPos && EndPos <= Arrow.endPos) || (Arrow.startPos <= StartPos && Arrow.startPos >= EndPos))
        {
            if (Margin <= Arrow.margin)
            {
                Margin = Arrow.margin + 1;
            }
        }
        if (StartPos == Arrow.startPos)
        {
            StartMargin = Arrow.startMargin + 1;
        }
        if (StartPos == Arrow.endPos)
        {
            StartMargin = Arrow.endMargin + 1;
        }
        if (EndPos == Arrow.startPos)
        {
            EndMargin = Arrow.startMargin + 1;
        }
        if (EndPos == Arrow.endPos)
        {
            EndMargin = Arrow.endMargin + 1;
        }
    }
    m_CommandList->Arrows.push_back({StartPos, EndPos, StartMargin, EndMargin, Margin});
}

void CommandsView::HistoryPushState()
{
    m_History.push_back(m_StartAddress);
    m_HistoryIndex = (int)(m_History.size() - 1);
    ToggleHistoryButtons();
}

void CommandsView::ToggleHistoryButtons()
{
    m_BackButton->setEnabled(m_History.size() != 0 && m_HistoryIndex > 0);
    m_ForwardButton->setEnabled(m_History.size() != 0 && m_HistoryIndex < (int)m_History.size() - 1);
}

// The annotation of an instruction, as shown in the Symbol column and copied by CopyCommands
namespace
{
struct Annotation
{
    std::string Text;
    bool LoadStore = false;
    bool IsSymbol = false;
};

// Returns the JAL symbol name for Parameters (if any) and the annotation of the row
void Annotate(CDebuggerUI * Debugger, uint32_t StartAddress, uint32_t OpAddr, COpInfo & OpInfo, std::string & CmdArgs, Annotation & Note)
{
    R4300iOpcode & OpCode = OpInfo.m_OpCode;
    CSymbol JalSymbol;
    // Show subroutine symbol name for JAL target
    if (OpCode.op == R4300i_JAL)
    {
        const uint32_t TargetAddr = (StartAddress & 0xF0000000) | (OpCode.target << 2);
        if (Debugger->SymbolTable()->GetSymbolByAddress(TargetAddr, &JalSymbol))
        {
            CmdArgs = JalSymbol.m_Name;
        }
    }

    // Detect reads and writes to mapped registers, cart header data, etc.
    const char * Text = nullptr;
    CSymbol MemSymbol;
    if (OpInfo.IsLoadStoreCommand())
    {
        for (int Offset = -4; Offset > -24; Offset -= 4)
        {
            R4300iOpcode OpCodeTest;
            if (!Debugger->DebugLoad_VAddr(OpAddr + Offset, OpCodeTest.Value))
            {
                break;
            }
            if (OpCodeTest.op != R4300i_LUI || OpCodeTest.rt != OpCode.rs)
            {
                continue;
            }
            const uint32_t MemAddr = (OpCodeTest.immediate << 16) + (short)OpCode.offset;
            if (Debugger->SymbolTable()->GetSymbolByAddress(MemAddr, &MemSymbol))
            {
                Text = MemSymbol.m_Name;
            }
            else
            {
                Text = GetDataAddressNotes(MemAddr);
            }
            break;
        }
    }
    if (Text == nullptr)
    {
        Text = GetCodeAddressNotes(OpAddr);
    }
    else
    {
        Note.LoadStore = true;
    }

    CSymbol PcSymbol;
    if (Debugger->SymbolTable()->GetSymbolByAddress(OpAddr, &PcSymbol))
    {
        Note.Text = PcSymbol.m_Name;
        Note.IsSymbol = true;
    }
    else if (Text != nullptr)
    {
        Note.Text = Text;
    }
}
} // namespace

void CommandsView::ShowAddress(uint32_t Address, bool Top, bool UserInput)
{
    if (Top)
    {
        m_StartAddress = Address - Address % 4;
        if (!UserInput)
        {
            m_AddressEdit->SetValue(Address);
        }
        if (!g_DebugSettings.stepping)
        {
            EnableStepButtons(false);
            m_RegisterTabs->SetColorsEnabled(false);
        }
    }
    else
    {
        const bool OutOfView = Address < m_StartAddress || Address > m_StartAddress + (m_CommandListRows - 1) * 4;
        if (OutOfView)
        {
            m_StartAddress = Address - Address % 4;
            m_AddressEdit->SetValue(Address);
        }
        if (m_History.size() == 0 || m_History[m_HistoryIndex] != m_StartAddress)
        {
            HistoryPushState();
        }
        m_PCEdit->SetValue((uint32_t)g_Reg->m_PROGRAM_COUNTER);
        EnableStepButtons(true);
        m_RegisterTabs->SetColorsEnabled(true);
    }

    m_CommandList->setUpdatesEnabled(false);
    m_CommandList->clearContents();
    m_CommandList->setRowCount(m_CommandListRows);
    m_CommandList->Arrows.clear();
    m_AnnotatedLines.clear();

    const uint32_t Pc = (g_Reg != nullptr) ? (uint32_t)g_Reg->m_PROGRAM_COUNTER : 0;
    R4300iOpcode PcOpcode;
    if (!m_Debugger->DebugLoad_VAddr(Pc, PcOpcode.Value))
    {
        PcOpcode.Value = 0;
    }

    auto Set = [this](int Row, int Col, const QString & Text, const QColor & Fg, const QColor & Bg) {
        QTableWidgetItem * Item = new QTableWidgetItem(Text);
        Item->setForeground(Fg);
        Item->setBackground(Bg);
        m_CommandList->setItem(Row, Col, Item);
    };

    for (int i = 0; i < m_CommandListRows; i++)
    {
        const uint32_t OpAddr = m_StartAddress + i * 4;

        // Address column colours (OnCustomDrawList, COL_ADDRESS)
        QColor AddrBg(0xEE, 0xEE, 0xEE), AddrFg(0x44, 0x44, 0x44);
        const CBreakpoints::BPSTATE BpState = m_Breakpoints->ExecutionBPExists(OpAddr);
        if (BpState == CBreakpoints::BP_SET)
        {
            AddrBg = QColor(0x44, 0x00, 0x00);
            AddrFg = (OpAddr == Pc && g_DebugSettings.debugging) ? QColor(0xFF, 0xFF, 0x00) : QColor(0xFF, 0xCC, 0xCC);
        }
        else if (BpState == CBreakpoints::BP_SET_TEMP)
        {
            AddrBg = QColor(0x66, 0x44, 0x00);
            AddrFg = (OpAddr == Pc && g_DebugSettings.debugging) ? QColor(0xFF, 0xFF, 0x00) : QColor(0xFF, 0xEE, 0xCC);
        }
        else if (OpAddr == Pc && g_DebugSettings.stepping)
        {
            AddrBg = QColor(0x88, 0x88, 0x88);
            AddrFg = QColor(0xFF, 0xFF, 0x00);
        }
        Set(i, CommandList::COL_ARROWS, " ", Qt::black, QColor(30, 30, 30));
        Set(i, CommandList::COL_ADDRESS, QString("%1").arg(OpAddr, 8, 16, QChar('0')).toUpper(), AddrFg, AddrBg);

        COpInfo OpInfo;
        R4300iOpcode & OpCode = OpInfo.m_OpCode;
        if (!m_Debugger->DebugLoad_VAddr(OpAddr, OpCode.Value))
        {
            Set(i, CommandList::COL_COMMAND, "***", QColor(0xFF, 0x00, 0x00), QColor(0xFF, 0xFF, 0xFF));
            m_AnnotatedLines.push_back(false);
            continue;
        }

        R4300iInstruction Instruction(OpAddr, OpCode.Value);
        std::string CmdArgs = Instruction.Param();
        Annotation Note;
        Annotate(m_Debugger, m_StartAddress, OpAddr, OpInfo, CmdArgs, Note);

        // Row colours
        uint32_t Fg, Bg;
        if (OpAddr == Pc && g_DebugSettings.stepping)
        {
            Bg = 0xFFFFAA, Fg = 0x222200;
        }
        else if (IsOpEdited(OpAddr))
        {
            Bg = 0xFFEEFF, Fg = 0xFF00FF;
        }
        else if (OpInfo.IsStackAlloc())
        {
            Bg = 0xCCDDFF, Fg = 0x001144;
        }
        else if (OpInfo.IsStackFree())
        {
            Bg = 0xFFDDDD, Fg = 0x440000;
        }
        else if (OpInfo.IsNOP())
        {
            Bg = 0xFFFFFF, Fg = 0x888888;
        }
        else if (OpInfo.IsJump())
        {
            Bg = 0xEEFFEE, Fg = 0x006600;
        }
        else if (OpInfo.IsBranch())
        {
            Bg = 0xFFFFFF, Fg = 0x337700;
        }
        else
        {
            Bg = 0xFFFFFF, Fg = 0x000000;
        }
        QColor BgColor(Bg), FgColor(Fg);
        QColor SymbolFg = FgColor;
        const bool Annotated = !Note.IsSymbol && !Note.Text.empty();
        if (Annotated)
        {
            SymbolFg = QColor(0x66, 0x66, 0x66); // Gray annotations
        }

        // Colour register usage (only while stepping)
        if (g_DebugSettings.stepping)
        {
            const QColor UsedRegister(0xF5, 0xF0, 0xFF), AffectedRegister(0xFF, 0xF0, 0xFF);
            int PcUsedRegA, PcUsedRegB = 0, PcChangedReg, CurUsedRegA, CurUsedRegB = 0, CurChangedReg;
            if (PcOpcode.op == R4300i_SPECIAL)
            {
                PcUsedRegA = PcOpcode.rs;
                PcUsedRegB = PcOpcode.rt;
                PcChangedReg = PcOpcode.rd;
            }
            else
            {
                PcUsedRegA = PcOpcode.rs;
                PcChangedReg = PcOpcode.rt;
            }
            if (OpCode.op == R4300i_SPECIAL)
            {
                CurUsedRegA = OpCode.rs;
                CurUsedRegB = OpCode.rt;
                CurChangedReg = OpCode.rd;
            }
            else
            {
                CurUsedRegA = OpCode.rs;
                CurChangedReg = OpCode.rt;
            }
            if (OpAddr < Pc)
            {
                if (CurChangedReg != 0 && (PcUsedRegA == CurChangedReg || PcUsedRegB == CurChangedReg))
                {
                    BgColor = UsedRegister;
                }
            }
            else if (OpAddr > Pc)
            {
                if (PcChangedReg != 0 && (CurUsedRegA == PcChangedReg || CurUsedRegB == PcChangedReg))
                {
                    BgColor = AffectedRegister;
                }
            }
        }

        Set(i, CommandList::COL_COMMAND, QString::fromStdString(Instruction.Name()), FgColor, BgColor);
        Set(i, CommandList::COL_PARAMETERS, QString::fromStdString(CmdArgs), FgColor, BgColor);
        QString SymbolText;
        if (Note.IsSymbol)
        {
            SymbolText = QString::fromStdString(Note.Text);
        }
        else if (Annotated)
        {
            SymbolText = QString(Note.LoadStore ? "// (%1)" : "// %1").arg(QString::fromStdString(Note.Text));
        }
        Set(i, CommandList::COL_SYMBOL, SymbolText, SymbolFg, BgColor);
        m_AnnotatedLines.push_back(Annotated);

        // Add arrow for branch instruction
        if (OpInfo.IsBranch())
        {
            const int StartPos = i;
            AddBranchArrow(StartPos, StartPos + (int16_t)OpCode.offset + 1);
        }
        // Branch arrow for close J
        if (OpCode.op == R4300i_J)
        {
            const uint32_t Target = (OpCode.target << 2);
            const int Dist = Target - (OpAddr & 0x3FFFFFF);
            if (abs(Dist) < 0x10000)
            {
                const int StartPos = i;
                AddBranchArrow(StartPos, StartPos + (Dist / 4));
            }
        }
    }

    if (!Top) // Update registers when called via breakpoint/stepping
    {
        m_RegisterTabs->RefreshEdits();
    }
    RefreshBreakpointList();
    m_CommandList->setUpdatesEnabled(true);
    m_CommandList->viewport()->update();
}

void CommandsView::CopyCommands(uint32_t Address, uint32_t Count)
{
    const size_t MaximumMnemonicLength = 7 + 1; // The + 1 gets us a space after the mnemonic
    std::string Buffer;
    const std::string Newline = "\r\n";
    const std::string NoteBuffer = "         "; // Used to distance the note from the args visually
    bool Any = false;
    for (uint32_t i = 0; i < Count; i++)
    {
        const uint32_t OpAddr = Address + i * 4;
        COpInfo OpInfo;
        R4300iOpcode & OpCode = OpInfo.m_OpCode;
        if (!m_Debugger->DebugLoad_VAddr(OpAddr, OpCode.Value))
        {
            continue;
        }
        if (Any)
        {
            Buffer.append(Newline);
        }
        Any = true;

        char AddrStr[10];
        snprintf(AddrStr, sizeof(AddrStr), "%08X ", OpAddr);
        Buffer.append(AddrStr);

        R4300iInstruction Instruction(OpAddr, OpCode.Value);
        std::string CmdArgs = Instruction.Param();
        Annotation Note;
        Annotate(m_Debugger, m_StartAddress, OpAddr, OpInfo, CmdArgs, Note);

        std::string Name = Instruction.Name();
        while (Name.length() < MaximumMnemonicLength)
        {
            Name.append(" ");
        }
        Buffer.append(Name);
        Buffer.append(CmdArgs);
        if (Note.IsSymbol)
        {
            Buffer.append(NoteBuffer);
            Buffer.append(Note.Text);
        }
        else if (!Note.Text.empty())
        {
            Buffer.append(NoteBuffer);
            Buffer.append(std::string(Note.LoadStore ? "// (" : "// ") + Note.Text + (Note.LoadStore ? ")" : ""));
        }
    }
    QApplication::clipboard()->setText(QString::fromStdString(Buffer));
}

void CommandsView::RefreshBreakpointList()
{
    m_BreakpointList->clear();
    auto Add = [this](const char Kind, const CBreakpoints::breakpoints_t & Points) {
        for (const auto & Point : Points)
        {
            QListWidgetItem * Item = new QListWidgetItem(QString("%1 %2%3").arg(Kind).arg(Point.second ? "T " : "").arg(Point.first, 8, 16, QChar('0')).toUpper());
            Item->setData(Qt::UserRole, Point.first);
            m_BreakpointList->addItem(Item);
        }
    };
    Add('R', m_Breakpoints->ReadMem());
    Add('W', m_Breakpoints->WriteMem());
    Add('E', m_Breakpoints->Execution());
}

void CommandsView::RemoveSelectedBreakpoints()
{
    QListWidgetItem * Item = m_BreakpointList->currentItem();
    if (Item == nullptr)
    {
        return;
    }
    const uint32_t Address = Item->data(Qt::UserRole).toUInt();
    switch (Item->text().at(0).toLatin1())
    {
    case 'E': m_Breakpoints->RemoveExecution(Address); break;
    case 'W': m_Breakpoints->WBPRemove(Address); break;
    case 'R': m_Breakpoints->RBPRemove(Address); break;
    }
    RefreshBreakpointList();
}

void CommandsView::ToggleBreakpointAt(uint32_t Address)
{
    if (m_Breakpoints->ExecutionBPExists(Address))
    {
        m_Breakpoints->RemoveExecution(Address);
    }
    else
    {
        m_Breakpoints->AddExecution(Address);
    }
    RefreshBreakpointList();
    ShowAddress(m_StartAddress, true);
}

void CommandsView::CPUSkip()
{
    g_Settings->SaveBool(Debugger_SkipOp, true);
    if (g_DebugSettings.waitingForStep)
    {
        m_StepEvent.Trigger();
    }
}

void CommandsView::CPUResume()
{
    g_Settings->SaveBool(Debugger_SteppingOps, false);
    if (g_DebugSettings.waitingForStep)
    {
        m_StepEvent.Trigger();
    }
}

void CommandsView::CPUStepOver()
{
    COpInfo OpInfo;
    if (g_Reg == nullptr || !m_Debugger->DebugLoad_VAddr((uint32_t)g_Reg->m_PROGRAM_COUNTER, OpInfo.m_OpCode.Value))
    {
        return;
    }
    if (OpInfo.IsJAL())
    {
        // Put temp breakpoints on return address and resume
        m_Breakpoints->AddExecution((uint32_t)g_Reg->m_PROGRAM_COUNTER + 8, true);
        CPUResume();
    }
    else
    {
        // Normal step
        if (g_DebugSettings.waitingForStep)
        {
            m_StepEvent.Trigger();
        }
    }
}

void CommandsView::StepClicked()
{
    if (g_DebugSettings.waitingForStep)
    {
        m_StepEvent.Trigger();
    }
}
void CommandsView::GoClicked()
{
    CPUResume();
}
void CommandsView::SkipClicked()
{
    CPUSkip();
}
void CommandsView::StepOverClicked()
{
    CPUStepOver();
}

void CommandsView::WaitingForStepChanged()
{
    if (g_DebugSettings.waitingForStep)
    {
        ShowAddress((uint32_t)g_Reg->m_PROGRAM_COUNTER, false);
        m_Debugger->Debug_RefreshStackWindow();
        m_Debugger->Debug_RefreshStackTraceWindow();
        m_StepButton->setEnabled(true);
        m_StepOverButton->setEnabled(true);
        m_GoButton->setEnabled(true);
        m_AddressEdit->setFocus();
    }
    else
    {
        m_StepButton->setEnabled(false);
        m_StepOverButton->setEnabled(false);
        m_GoButton->setEnabled(false);
    }
}

void CommandsView::SteppingOpsChanged()
{
    if (!g_Settings->LoadBool(Debugger_SteppingOps))
    {
        m_Debugger->Debug_RefreshStackWindow();
        m_Debugger->Debug_RefreshStackTraceWindow();
        m_RegisterTabs->SetColorsEnabled(false);
        m_RegisterTabs->RefreshEdits();
        ShowAddress(m_StartAddress, true);
    }
}

void CommandsView::Reset()
{
    m_EditedOps.clear();
    m_History.clear();
    ToggleHistoryButtons();
}

void CommandsView::ShowPIRegTab()
{
    m_RegisterTabs->setCurrentIndex(2);
}

// The context menu of a command (IDR_OP_POPUP)
void CommandsView::ShowContextMenu(int Row, const QPoint & GlobalPos)
{
    EndOpEdit();
    m_SelectedAddress = m_StartAddress + Row * 4;
    COpInfo OpInfo;
    R4300iOpcode & OpCode = OpInfo.m_OpCode;
    if (!m_Debugger->DebugLoad_VAddr(m_SelectedAddress, OpCode.Value))
    {
        return;
    }
    bool CanFollow = true, CanViewMemory = true;
    if (OpInfo.IsStaticJump())
    {
        m_FollowAddress = (m_SelectedAddress & 0xF0000000) | (OpCode.target * 4);
    }
    else if (OpInfo.IsBranch())
    {
        m_FollowAddress = m_SelectedAddress + ((int16_t)OpCode.offset + 1) * 4;
    }
    else
    {
        CanFollow = false;
    }
    if (OpInfo.IsLoadStoreCommand())
    {
        m_FollowAddress = g_Reg->m_GPR[OpCode.base].UW[0] + (int16_t)OpCode.offset;
    }
    else
    {
        CanViewMemory = false;
    }

    QMenu Menu(this);
    Menu.addAction(Tr("Run to line"), [this]() {
        // Add temp breakpoints and resume
        m_Breakpoints->AddExecution(m_SelectedAddress, true);
    });
    Menu.addAction(Tr("Toggle breakpoint"), [this]() { ToggleBreakpointAt(m_SelectedAddress); });
    Menu.addAction(Tr("Clear breakpoints"), [this]() {
        m_Breakpoints->EBPClear();
        RefreshBreakpointList();
        ShowAddress(m_StartAddress, true);
    })->setEnabled(m_Breakpoints->Execution().size() != 0);
    Menu.addSeparator();
    Menu.addAction(Tr("Follow"), [this]() {
        HistoryPushState();
        ShowAddress(m_FollowAddress, true);
        HistoryPushState();
    })->setEnabled(CanFollow);
    Menu.addAction(Tr("View memory..."), [this]() { m_Debugger->Debug_ShowMemoryLocation(m_FollowAddress, true); })->setEnabled(CanViewMemory);
    Menu.addSeparator();
    Menu.addAction(Tr("Add symbol..."), [this]() { DebugDialogs::AddSymbol(m_Debugger, this, m_SelectedAddress, SYM_CODE); });
    Menu.addSeparator();
    Menu.addAction(Tr("Replace with NOP"), [this]() {
        EditOp(m_SelectedAddress, 0x00000000);
        ShowAddress(m_StartAddress, true);
    })->setEnabled(!OpInfo.IsNOP());
    Menu.addAction(Tr("Copy commands..."), [this]() { DebugDialogs::CopyCommands(m_Debugger, this, m_SelectedAddress); });
    Menu.addAction(Tr("Edit code"), [this]() { BeginOpEdit(m_SelectedAddress); });
    Menu.addAction(Tr("Restore code"), [this]() {
        RestoreOp(m_SelectedAddress);
        ShowAddress(m_StartAddress, true);
    })->setEnabled(IsOpEdited(m_SelectedAddress));
    Menu.addAction(Tr("Restore all code"), [this]() {
        RestoreAllOps();
        ShowAddress(m_StartAddress, true);
    })->setEnabled(m_EditedOps.size() != 0);
    Menu.exec(GlobalPos);
}

// Opcode editor

void CommandsView::BeginOpEdit(uint32_t Address)
{
    uint32_t Opcode;
    if (!m_Debugger->DebugLoad_VAddr(Address, Opcode))
    {
        return;
    }
    m_Editing = true;
    const int Row = (Address - m_StartAddress) / 4;
    m_SelectedAddress = Address;
    QRect Rect = m_CommandList->visualRect(m_CommandList->model()->index(std::max(0, std::min(Row, m_CommandListRows - 1)), CommandList::COL_COMMAND));
    Rect.setRight(Rect.left() + 200);
    m_OpEdit->setGeometry(Rect);
    m_OpEdit->setText(QString::fromStdString(R4300iInstruction(Address, Opcode).NameAndParam()));
    m_OpEdit->show();
    m_OpEdit->raise();
    m_OpEdit->setFocus();
    m_OpEdit->selectAll();
}

void CommandsView::EndOpEdit()
{
    m_Editing = false;
    m_OpEdit->clear();
    m_OpEdit->hide();
}

// OnOpEditKeyDown
bool CommandsView::HandleOpEditKey(int Key)
{
    if (Key == Qt::Key_Up)
    {
        m_SelectedAddress -= 4;
        BeginOpEdit(m_SelectedAddress);
        return true;
    }
    if (Key == Qt::Key_Down)
    {
        m_SelectedAddress += 4;
        BeginOpEdit(m_SelectedAddress);
        return true;
    }
    if (Key == Qt::Key_Return || Key == Qt::Key_Enter)
    {
        OpEditReturn();
        return true;
    }
    if (Key == Qt::Key_Escape)
    {
        EndOpEdit();
        return true;
    }
    return false;
}

void CommandsView::OpEditReturn()
{
    uint32_t Op;
    const std::string Text = m_OpEdit->text().toStdString();
    if (CAssembler::AssembleLine(Text.c_str(), &Op, m_SelectedAddress))
    {
        m_OpEdit->clear();
        EditOp(m_SelectedAddress, Op);
        m_SelectedAddress += 4;
        BeginOpEdit(m_SelectedAddress);
    }
}

void CommandsView::AssembleAndEdit(uint32_t Address, const QString & Line)
{
    m_SelectedAddress = Address;
    m_OpEdit->setText(Line);
    OpEditReturn();
}

// OnOpEditChanged: multiline input assembles every line
void CommandsView::OpEditMultiline(QString Text)
{
    EndOpEdit();
    Text.replace('\r', '\n');
    const QStringList Lines = Text.split('\n', Qt::SkipEmptyParts);
    for (const QString & Line : Lines)
    {
        uint32_t Op;
        const std::string Std = Line.toStdString();
        if (CAssembler::AssembleLine(Std.c_str(), &Op, m_SelectedAddress))
        {
            EditOp(m_SelectedAddress, Op, false);
            m_SelectedAddress += 4;
        }
        else
        {
            ShowAddress(m_StartAddress, true);
            BeginOpEdit(m_SelectedAddress);
            m_OpEdit->setText(Line);
            return;
        }
    }
    ShowAddress(m_StartAddress, true);
    BeginOpEdit(m_SelectedAddress);
}

bool CommandsView::IsOpEdited(uint32_t Address) const
{
    for (const EditedOp & Edited : m_EditedOps)
    {
        if (Edited.address == Address)
        {
            return true;
        }
    }
    return false;
}

void CommandsView::EditOp(uint32_t Address, uint32_t Op, bool Refresh)
{
    uint32_t CurrentOp;
    if (!m_Debugger->DebugLoad_VAddr(Address, CurrentOp))
    {
        return;
    }
    if (CurrentOp == Op)
    {
        return;
    }
    m_Debugger->DebugStore_VAddr(Address, Op);
    if (!IsOpEdited(Address))
    {
        m_EditedOps.push_back({Address, CurrentOp});
    }
    if (Refresh)
    {
        ShowAddress(m_StartAddress, true);
    }
}

void CommandsView::RestoreOp(uint32_t Address)
{
    for (size_t i = 0; i < m_EditedOps.size(); i++)
    {
        if (m_EditedOps[i].address == Address)
        {
            m_Debugger->DebugStore_VAddr(m_EditedOps[i].address, m_EditedOps[i].originalOp);
            m_EditedOps.erase(m_EditedOps.begin() + i);
            break;
        }
    }
}

void CommandsView::RestoreAllOps()
{
    for (int i = (int)m_EditedOps.size() - 1; i >= 0; i--)
    {
        m_Debugger->DebugStore_VAddr(m_EditedOps[i].address, m_EditedOps[i].originalOp);
        m_EditedOps.erase(m_EditedOps.begin() + i);
    }
}
