#include "stdafx.h"
#include "../Vortex/Strings.h"

#include "CpuLogView.h"
#include "DebugCommon.h"
#include "DebuggerUI.h"

#include <Project64-core/N64System/Mips/R4300iInstruction.h>
#include <Project64-core/N64System/Mips/Register.h>

#include <QCheckBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QWheelEvent>

using Vortex::Tr;

CpuLogView::CpuLogView(CDebuggerUI * Debugger) :
    DebugWindow(Debugger, DebuggerUI_CPULogPos, true)
{
    setWindowTitle(Tr("CPU Log"));
    resize(423 * 1.6, 267 * 1.6);
    QGridLayout * Layout = new QGridLayout(this);
    m_Enabled = new QCheckBox(Tr("Enable CPU Logging"), this);
    m_BufferSize = new NumberEdit(this, NumberEdit::DisplayDec, 10);
    m_BufferSize->setFixedWidth(80);
    m_Export = new QPushButton(Tr("Export"), this);
    QHBoxLayout * Top = new QHBoxLayout;
    Top->addWidget(m_Enabled);
    Top->addStretch();
    Top->addWidget(new QLabel(Tr("Buffer size:"), this));
    Top->addWidget(m_BufferSize);
    Top->addStretch();
    Top->addWidget(m_Export);
    Layout->addLayout(Top, 0, 0, 1, 3);

    m_List = new QTableWidget(0, 3, this);
    m_List->setHorizontalHeaderLabels({"PC", Tr("Command"), Tr("Parameters")});
    m_List->verticalHeader()->hide();
    m_List->verticalHeader()->setDefaultSectionSize(m_List->fontMetrics().height() + 2);
    m_List->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_List->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_List->setSelectionMode(QAbstractItemView::SingleSelection);
    m_List->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_List->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_List->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_List->setColumnWidth(0, 90);
    m_List->setColumnWidth(1, 80);
    m_List->horizontalHeader()->setStretchLastSection(true);
    Layout->addWidget(m_List, 1, 0);
    m_Scrollbar = new QScrollBar(Qt::Vertical, this);
    Layout->addWidget(m_Scrollbar, 1, 1);
    QGroupBox * Registers = new QGroupBox(Tr("Registers"), this);
    QGridLayout * RegLayout = new QGridLayout(Registers);
    m_StateInfo = new QPlainTextEdit(Registers);
    m_StateInfo->setReadOnly(true);
    m_StateInfo->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_StateInfo->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    RegLayout->addWidget(m_StateInfo);
    Layout->addWidget(Registers, 1, 2);
    Layout->setColumnStretch(0, 2);
    Layout->setColumnStretch(2, 3);

    const bool LoggingEnabled = g_Settings->LoadBool(Debugger_CPULoggingEnabled);
    const uint32_t BufferSize = g_Settings->LoadDword(Debugger_CPULogBufferSize);
    m_Enabled->setChecked(LoggingEnabled);
    m_BufferSize->SetValue(BufferSize);
    m_BufferSize->setEnabled(!LoggingEnabled);
    RefreshList(true);
    m_Export->setEnabled(false);
    if (!LoggingEnabled && m_CPULogCopy != nullptr)
    {
        m_Export->setEnabled(true);
    }

    connect(m_Enabled, &QCheckBox::clicked, this, [this]() { ToggleLoggingEnabled(); });
    connect(m_Export, &QPushButton::clicked, this, [this]() {
        if (m_CPULogCopy == nullptr)
        {
            return;
        }
        const QString Path = QFileDialog::getSaveFileName(this, QString(), "CPULOG.txt", Tr("CPU Log (*.*)"));
        if (!Path.isEmpty())
        {
            Export(Path);
        }
    });
    connect(m_Scrollbar, &QScrollBar::actionTriggered, this, [this](int Action) {
        // WM_VSCROLL: line up/down, thumb track
        int NewPos;
        switch (Action)
        {
        case QAbstractSlider::SliderSingleStepSub: NewPos = std::max(m_Scrollbar->minimum(), m_Scrollbar->value() - 1); break;
        case QAbstractSlider::SliderSingleStepAdd: NewPos = std::min(m_Scrollbar->maximum(), m_Scrollbar->value() + 1); break;
        case QAbstractSlider::SliderMove: NewPos = m_Scrollbar->sliderPosition(); break;
        default: return;
        }
        ScrollTo(NewPos);
    });
    connect(m_List, &QTableWidget::currentCellChanged, this, [this](int Row, int, int, int) {
        if (Row >= 0 && m_CPULogCopy != nullptr)
        {
            ShowRegStates(m_LogStartIndex + Row);
        }
    });
    connect(m_List, &QTableWidget::cellDoubleClicked, this, [this](int Row, int) {
        if (m_CPULogCopy == nullptr)
        {
            return;
        }
        CPUState * State = m_CPULogCopy->GetEntry(m_LogStartIndex + Row);
        if (State != nullptr)
        {
            m_Debugger->Debug_ShowCommandsLocation(State->pc, true);
        }
    });
    LoadWindowPos();
}

CpuLogView::~CpuLogView()
{
    delete m_CPULogCopy;
}

void CpuLogView::ScrollTo(int Position)
{
    m_LogStartIndex = Position;
    m_Scrollbar->setValue(Position);
    RefreshList(false);
}

// InterceptMouseWheel: only over the list or the scroll bar
void CpuLogView::wheelEvent(QWheelEvent * Event)
{
    const int Scroll = -(Event->angleDelta().y() / 120);
    const QPoint Pos = Event->position().toPoint();
    if (Scroll != 0 && (m_List->geometry().contains(Pos) || m_Scrollbar->geometry().contains(Pos)))
    {
        ScrollTo(std::max(m_Scrollbar->minimum(), std::min(m_Scrollbar->maximum(), m_Scrollbar->value() + Scroll)));
    }
    Event->accept();
}

void CpuLogView::resizeEvent(QResizeEvent * Event)
{
    DebugWindow::resizeEvent(Event);
    RefreshList(false);
}

void CpuLogView::showEvent(QShowEvent * Event)
{
    DebugWindow::showEvent(Event);
    RefreshList(false);
}

int CpuLogView::NumVisibleRows()
{
    return m_List->viewport()->height() / m_List->verticalHeader()->defaultSectionSize();
}

void CpuLogView::ToggleLoggingEnabled()
{
    const bool EnableLogging = m_Enabled->isChecked();
    m_BufferSize->setEnabled(!EnableLogging);
    g_Settings->SaveBool(Debugger_CPULoggingEnabled, EnableLogging);
    if (EnableLogging)
    {
        g_Settings->SaveDword(Debugger_CPULogBufferSize, m_BufferSize->Value());
        m_Debugger->CPULog()->Reset();
        m_Export->setEnabled(false);
    }
    else
    {
        RefreshList(true);
        m_Export->setEnabled(m_CPULogCopy != nullptr);
    }
}

void CpuLogView::RefreshList(bool UpdateBuffer)
{
    if (UpdateBuffer)
    {
        delete m_CPULogCopy;
        m_CPULogCopy = m_Debugger->CPULog()->Clone();
    }
    if (m_CPULogCopy == nullptr)
    {
        return;
    }
    const size_t Count = m_CPULogCopy->GetCount();
    const size_t VisibleRows = (size_t)std::max(NumVisibleRows(), 0);
    const bool CanDisplayAll = (VisibleRows >= Count);
    const int ScrollRangeMax = CanDisplayAll ? 0 : (int)(Count - VisibleRows);
    m_Scrollbar->setRange(0, ScrollRangeMax);
    m_Scrollbar->setEnabled(!CanDisplayAll);
    if (UpdateBuffer)
    {
        m_LogStartIndex = ScrollRangeMax;
        m_Scrollbar->setValue(m_LogStartIndex);
        if (Count > 0)
        {
            ShowRegStates(Count - 1);
        }
    }
    const size_t Start = m_Scrollbar->value();
    size_t End = Start + VisibleRows;
    if (End > Count)
    {
        End = Count;
    }
    m_List->setUpdatesEnabled(false);
    m_List->clearContents();
    m_List->setRowCount((int)(End > Start ? End - Start : 0));
    int Item = 0;
    for (size_t i = Start; i < End; i++)
    {
        CPUState * State = m_CPULogCopy->GetEntry(i);
        if (State == nullptr)
        {
            break;
        }
        R4300iInstruction Instruction(State->pc, State->opcode.Value);
        m_List->setItem(Item, 0, new QTableWidgetItem(QString("%1").arg(State->pc, 8, 16, QChar('0')).toUpper()));
        m_List->setItem(Item, 1, new QTableWidgetItem(QString::fromStdString(Instruction.Name())));
        m_List->setItem(Item, 2, new QTableWidgetItem(QString::fromStdString(Instruction.Param())));
        Item++;
    }
    m_List->setUpdatesEnabled(true);
}

void CpuLogView::ShowRegStates(size_t StateIndex)
{
    CPUState * State = m_CPULogCopy->GetEntry(StateIndex);
    if (State == nullptr)
    {
        return;
    }
    char Text[2048];
    char * Out = Text;
    Out += sprintf(Out, "PC: %08X\r\n\r\n", State->pc);
    for (int i = 0; i < 16; i++)
    {
        int RegL = i, RegR = i + 16;
        Out += sprintf(Out, "%s: %08X %08X  %s: %08X %08X\r\n",
                       CRegName::GPR[RegL], State->gpr[RegL].UW[1], State->gpr[RegL].UW[0],
                       CRegName::GPR[RegR], State->gpr[RegR].UW[1], State->gpr[RegR].UW[0]);
    }
    Out += sprintf(Out, "HI: %08X %08X  LO: %08X %08X\r\n\r\n",
                   State->gprHi.UW[1], State->gprHi.UW[0],
                   State->gprLo.UW[1], State->gprLo.UW[0]);
    for (int i = 0; i < 16; i++)
    {
        int RegL = i, RegR = i + 16;
        Out += sprintf(Out, "%-3s: %08X  %-3s: %08X\r\n",
                       CRegName::FPR[RegL], *(uint32_t *)&State->fpr[RegL],
                       CRegName::FPR[RegR], *(uint32_t *)&State->fpr[RegR]);
    }
    Out += sprintf(Out, "FPCR: %08X\r\n", State->fpcr);
    m_StateInfo->setPlainText(QString::fromLatin1(Text).replace("\r\n", "\n"));
}

bool CpuLogView::Export(const QString & Path)
{
    if (m_CPULogCopy == nullptr)
    {
        return false;
    }
    m_CPULogCopy->DumpToFile(Path.toLocal8Bit().constData());
    return true;
}
