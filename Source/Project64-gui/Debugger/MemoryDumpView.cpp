#include "stdafx.h"
#include "../Vortex/Strings.h"

#include "MemoryDumpView.h"
#include "DebugCommon.h"
#include "DebuggerUI.h"

#include <Common/File.h>
#include <Common/Log.h>
#include <Project64-core/N64System/Mips/R4300iInstruction.h>

#include <QCheckBox>
#include <QFileInfo>
#include <QComboBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <memory>

using Vortex::Tr;

// The pause/resume pair only makes sense while the CPU runs: while it waits in the debugger
// the pause request stays queued and is processed after the matching resume, leaving the
// emulation paused after Go (upstream has the same sequence and the same defect)
static void PauseCPU()
{
    if (g_BaseSystem != nullptr && !g_DebugSettings.waitingForStep)
    {
        g_BaseSystem->ExternalEvent(SysEvent_PauseCPU_DumpMemory);
    }
}

static void ResumeCPU()
{
    if (g_BaseSystem != nullptr && !g_DebugSettings.waitingForStep)
    {
        g_BaseSystem->ExternalEvent(SysEvent_ResumeCPU_DumpMemory);
    }
}

MemoryDumpView::MemoryDumpView(CDebuggerUI * Debugger) :
    DebugWindow(Debugger, DebuggerUI_MemoryDumpPos, false)
{
    setWindowTitle(Tr("Dump Memory"));
    QGridLayout * Layout = new QGridLayout(this);
    QGroupBox * Range = new QGroupBox(Tr("Range"), this);
    QGridLayout * RangeLayout = new QGridLayout(Range);
    m_StartAddress = new NumberEdit(Range, NumberEdit::DisplayHex);
    m_EndAddress = new NumberEdit(Range, NumberEdit::DisplayHex);
    m_PC = new NumberEdit(Range, NumberEdit::DisplayHex);
    m_UseAltPC = new QCheckBox(Tr("Use alternate PC:"), Range);
    RangeLayout->addWidget(new QLabel(Tr("&Start VAddress"), Range), 0, 0);
    RangeLayout->addWidget(m_StartAddress, 0, 1);
    RangeLayout->addWidget(new QLabel(Tr("&End VAddress:"), Range), 1, 0);
    RangeLayout->addWidget(m_EndAddress, 1, 1);
    RangeLayout->addWidget(m_UseAltPC, 2, 0);
    RangeLayout->addWidget(m_PC, 2, 1);
    Layout->addWidget(Range, 0, 0, 1, 2);

    QGroupBox * File = new QGroupBox(Tr("File"), this);
    QGridLayout * FileLayout = new QGridLayout(File);
    m_FileName = new QLineEdit(File);
    m_Choose = new QPushButton("...", File);
    m_Choose->setFixedWidth(30);
    m_Format = new QComboBox(File);
    FileLayout->addWidget(new QLabel(Tr("Filename:"), File), 0, 0);
    FileLayout->addWidget(m_FileName, 0, 1);
    FileLayout->addWidget(m_Choose, 0, 2);
    FileLayout->addWidget(new QLabel(Tr("Format:"), File), 1, 0);
    FileLayout->addWidget(m_Format, 1, 1, 1, 2);
    Layout->addWidget(File, 1, 0, 1, 2);
    m_Dump = new QPushButton(Tr("Dump"), this);
    m_Dump->setDefault(true);
    m_Close = new QPushButton(Tr("Close"), this);
    Layout->addWidget(m_Dump, 2, 0);
    Layout->addWidget(m_Close, 2, 1);

    const uint32_t StartAddress = 0x80000000;
    const uint32_t EndAddress = StartAddress + (g_MMU ? g_MMU->RdramSize() : 0x400000);
    m_StartAddress->SetValue(StartAddress);
    m_EndAddress->SetValue(EndAddress);
    m_PC->SetValue(StartAddress);
    m_Format->addItem(Tr("TEXT - Disassembly + PC"), (int)DisassemblyWithPC);
    m_Format->addItem(Tr("RAW - Big Endian (N64)"), (int)RawBigEndian);
    m_Format->setCurrentIndex(0);

    connect(m_Close, &QPushButton::clicked, this, [this]() { hide(); });
    connect(m_Choose, &QPushButton::clicked, this, [this]() { ChooseFile(); });
    connect(m_Dump, &QPushButton::clicked, this, [this]() { DoDump(); });
    LoadWindowPos();
}

void MemoryDumpView::ChooseFile()
{
    PauseCPU();
    const DumpFormat Format = (DumpFormat)m_Format->currentData().toInt();
    QString Filter = Tr("All files (*.*)");
    if (Format == RawBigEndian)
    {
        Filter = Tr("Binary file (*.bin);;All files (*.*)");
    }
    else if (Format == DisassemblyWithPC)
    {
        Filter = Tr("Text file (*.txt);;All files (*.*)");
    }
    QString Selected = QFileDialog::getSaveFileName(this, QString(), QString(), Filter);
    if (!Selected.isEmpty())
    {
        // upstream only fills the name in when the chosen file has no extension (and then adds
        // one), which leaves the box empty for "dump.bin"; the chosen file is always shown here
        if (!QFileInfo(Selected).suffix().length())
        {
            Selected += Format == RawBigEndian ? ".bin" : ".txt";
        }
        m_FileName->setText(Selected);
    }
    ResumeCPU();
}

bool MemoryDumpView::DoDump()
{
    const DumpFormat Format = (DumpFormat)m_Format->currentData().toInt();
    const uint32_t StartPC = m_StartAddress->Value();
    const uint32_t EndPC = m_EndAddress->Value();
    uint32_t DumpPC = m_PC->Value();
    const QString FileName = m_FileName->text();
    if (FileName.isEmpty())
    {
        g_Notify->DisplayWarning(Tr("Please choose target file").toUtf8().constData());
        m_FileName->setFocus();
        return false;
    }
    if (!m_UseAltPC->isChecked())
    {
        DumpPC = StartPC;
    }
    // Disable the controls while dumping
    for (QWidget * Widget : std::initializer_list<QWidget *>{m_StartAddress, m_EndAddress, m_PC, m_UseAltPC, m_FileName, m_Choose, m_Format, m_Dump, m_Close})
    {
        Widget->setEnabled(false);
    }
    bool Result = false;
    PauseCPU();
    Result = DumpMemory(FileName, Format, StartPC, EndPC, DumpPC);
    ResumeCPU();
    for (QWidget * Widget : std::initializer_list<QWidget *>{m_StartAddress, m_EndAddress, m_PC, m_UseAltPC, m_FileName, m_Choose, m_Format, m_Dump, m_Close})
    {
        Widget->setEnabled(true);
    }
    if (Result)
    {
        hide(); // EndDialog
    }
    return Result;
}

bool MemoryDumpView::DumpMemory(const QString & FileName, DumpFormat Format, uint32_t StartPC, uint32_t EndPC, uint32_t DumpPC)
{
    const std::string File = FileName.toStdString();
    if (Format == DisassemblyWithPC)
    {
        CLog LogFile;
        if (!LogFile.Open(File.c_str()))
        {
            g_Notify->DisplayError((Tr("Failed to open\n%1").arg(QString::fromStdString(File))).toUtf8().constData());
            return false;
        }
        LogFile.SetFlush(false);
        LogFile.SetTruncateFile(false);
        for (uint32_t pc = StartPC; pc < EndPC; pc += 4, DumpPC += 4)
        {
            R4300iOpcode Opcode;
            m_Debugger->DebugLoad_VAddr(pc, Opcode.Value);
            R4300iInstruction Instruction(DumpPC, Opcode.Value);
            LogFile.LogF("%X: %-15s%s\r\n", DumpPC, Instruction.Name(), Instruction.Param());
        }
        m_StartAddress->SetValue(StartPC);
        m_EndAddress->SetValue(EndPC);
        return true;
    }
    if (Format == RawBigEndian)
    {
        CFile DumpFile;
        if (!DumpFile.Open(File.c_str(), CFile::modeCreate | CFile::modeWrite))
        {
            g_Notify->DisplayError((Tr("Failed to open\n%1").arg(QString::fromStdString(File))).toUtf8().constData());
            return false;
        }
        const uint32_t DumpLen = EndPC - StartPC;
        std::unique_ptr<uint8_t[]> DumpBuf = std::make_unique<uint8_t[]>(DumpLen);
        uint32_t DumpIdx = 0;
        for (uint32_t pc = StartPC; pc < EndPC; pc++, DumpIdx++)
        {
            const bool Readable = m_Debugger->DebugLoad_VAddr(pc, DumpBuf.get()[DumpIdx]);
            if (!Readable)
            {
                g_Notify->DisplayError((Tr("Address error\n%1").arg(QString::fromStdString(File))).toUtf8().constData());
                DumpFile.Close();
                return false;
            }
        }
        DumpFile.SeekToBegin();
        DumpFile.Write(DumpBuf.get(), DumpLen);
        DumpFile.Close();
        return true;
    }
    return false;
}
