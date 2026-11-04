#include "stdafx.h"
#include "../Vortex/Strings.h"

#include "DmaLogView.h"
#include "DebugCommon.h"
#include "DebuggerUI.h"

#include <Project64-core/N64System/N64Rom.h>

#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollBar>
#include <QTableWidget>
#include <fstream>

using Vortex::Tr;

DmaLogView::DmaLogView(CDebuggerUI * Debugger) :
    DebugWindow(Debugger, DebuggerUI_DMALogPos, true),
    m_DMALog(Debugger->DMALog())
{
    setWindowTitle(Tr("DMA Log"));
    resize(309 * 1.6, 215 * 1.6);
    QGridLayout * Layout = new QGridLayout(this);
    m_List = new QTableWidget(0, 5, this);
    m_List->setHorizontalHeaderLabels({"ROM", "RAM", Tr("Length"), Tr("Symbol (RAM)"), Tr("Signature")});
    m_List->verticalHeader()->hide();
    m_List->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_List->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_List->setColumnWidth(0, 90);
    m_List->setColumnWidth(1, 90);
    m_List->setColumnWidth(2, 150);
    m_List->setColumnWidth(3, 120);
    m_List->horizontalHeader()->setStretchLastSection(true);
    Layout->addWidget(m_List, 0, 0, 1, 3);

    QGroupBox * Trace = new QGroupBox(Tr("Trace"), this);
    QGridLayout * TraceLayout = new QGridLayout(Trace);
    m_RamEdit = new QLineEdit(Trace);
    m_RomEdit = new QLineEdit(Trace);
    m_RamEdit->setMaxLength(8);
    m_RomEdit->setMaxLength(8);
    m_BlockInfo = new QLabel(Tr("Block:"), Trace);
    TraceLayout->addWidget(new QLabel("RAM", Trace), 0, 0);
    TraceLayout->addWidget(m_RamEdit, 0, 1);
    TraceLayout->addWidget(new QLabel("ROM", Trace), 0, 2);
    TraceLayout->addWidget(m_RomEdit, 0, 3);
    TraceLayout->addWidget(m_BlockInfo, 1, 0, 1, 4);
    Layout->addWidget(Trace, 1, 0);
    m_ClearButton = new QPushButton(Tr("Clear"), this);
    m_ExportButton = new QPushButton(Tr("Export..."), this);
    m_ExportButton->setEnabled(false);
    Layout->addWidget(m_ClearButton, 1, 1);
    Layout->addWidget(m_ExportButton, 1, 2);
    Layout->setColumnStretch(0, 1);

    connect(m_ClearButton, &QPushButton::clicked, this, [this]() {
        m_DMALog->ClearEntries();
        RefreshList();
    });
    connect(m_ExportButton, &QPushButton::clicked, this, [this]() {
        const QString Path = QFileDialog::getSaveFileName(this, QString(), "*.csv", Tr("Comma separated values (*.csv);;Plain text (*.txt)"));
        if (!Path.isEmpty())
        {
            Export(Path);
        }
    });
    connect(m_RamEdit, &QLineEdit::textEdited, this, [this]() { RamAddrChanged(); });
    connect(m_RomEdit, &QLineEdit::textEdited, this, [this]() { RomAddrChanged(); });
    RefreshList();
    LoadWindowPos();
}

void DmaLogView::RefreshDMALogWindow(bool Reset)
{
    if (Reset)
    {
        m_DMALog->ClearEntries();
    }
    if (m_RefreshPending.exchange(true))
    {
        return; // a refresh is already queued: it reads the whole log
    }
    QPointer<DmaLogView> Guard(this);
    RunOnGuiThread([Guard]() {
        if (Guard)
        {
            Guard->m_RefreshPending = false;
            Guard->RefreshList();
        }
    });
}

void DmaLogView::RefreshList()
{
    if (g_Rom == nullptr)
    {
        return;
    }
    uint8_t * Rom = g_Rom->GetRomAddress();
    const bool ScrolledDown = m_List->verticalScrollBar()->value() == m_List->verticalScrollBar()->maximum();
    const int DmaLogSize = (int)m_DMALog->GetNumEntries();
    int StartIndex;
    if (DmaLogSize == 0)
    {
        // Reset
        m_List->setRowCount(0);
        StartIndex = 0;
        m_ExportButton->setEnabled(false);
    }
    else
    {
        // Continue from last index
        StartIndex = m_LastStartIndex;
        m_ExportButton->setEnabled(true);
    }
    m_List->setUpdatesEnabled(false);
    int ItemIndex = m_List->rowCount();
    for (int i = StartIndex; i < DmaLogSize; i++)
    {
        DMALOGENTRY * Entry = m_DMALog->GetEntryByIndex(i);
        m_List->setRowCount(ItemIndex + 1);
        auto Hex = [](uint32_t Value) { return QString("%1").arg(Value, 8, 16, QChar('0')).toUpper(); };
        m_List->setItem(ItemIndex, 0, new QTableWidgetItem(Hex(Entry->romAddr)));
        m_List->setItem(ItemIndex, 1, new QTableWidgetItem(Hex(Entry->ramAddr)));
        m_List->setItem(ItemIndex, 2, new QTableWidgetItem(QString("%1 (%2)").arg(Hex(Entry->length)).arg(Entry->length)));
        union
        {
            uint32_t u32;
            uint8_t sz[5];
        } Sig = {0};
        if (Entry->romAddr < g_Rom->GetRomSize())
        {
            Sig.u32 = _byteswap_ulong(*(uint32_t *)&Rom[Entry->romAddr]);
        }
        // TODO (upstream): checkbox to display all in hex
        if (isalnum(Sig.sz[0]) && isalnum(Sig.sz[1]) && isalnum(Sig.sz[2]) && isalnum(Sig.sz[3]))
        {
            m_List->setItem(ItemIndex, 4, new QTableWidgetItem(QString::fromLatin1((char *)Sig.sz)));
        }
        // The list colours (OnCustomDrawList): continuation of the previous block / head of a block
        QColor Background;
        if (ItemIndex >= 1)
        {
            DMALOGENTRY * Prev = m_DMALog->GetEntryByIndex(ItemIndex - 1);
            if (Entry->romAddr == Prev->romAddr + Prev->length)
            {
                Background = QColor(0xFF, 0xFF, 0xCC);
            }
        }
        if (Background.isValid())
        {
            m_List->item(ItemIndex, 0)->setBackground(Background);
        }
        if (ItemIndex >= 1)
        {
            // the previous row is the head of a block when this one continues it
            DMALOGENTRY * Prev = m_DMALog->GetEntryByIndex(ItemIndex - 1);
            if (Entry->romAddr == Prev->romAddr + Prev->length)
            {
                QTableWidgetItem * PrevItem = m_List->item(ItemIndex - 1, 0);
                if (PrevItem != nullptr && PrevItem->background().color() != QColor(0xFF, 0xFF, 0xCC))
                {
                    PrevItem->setBackground(QColor(0xFF, 0xFF, 0x88));
                }
            }
        }
        ItemIndex++;
    }
    if (ScrolledDown)
    {
        m_List->scrollToBottom();
    }
    m_List->setUpdatesEnabled(true);
    m_LastStartIndex = DmaLogSize;
}

bool DmaLogView::Export(const QString & Path)
{
    QString FilePath = Path;
    if (!FilePath.contains('.'))
    {
        FilePath += ".csv";
    }
    std::ofstream File(FilePath.toStdString(), std::ios::out | std::ios::binary);
    if (!File.is_open())
    {
        return false;
    }
    File << "ROM Address,RAM Address,Length\r\n";
    const size_t NumEntries = m_DMALog->GetNumEntries();
    for (size_t Entry = 0; Entry < NumEntries; Entry++)
    {
        DMALOGENTRY * E = m_DMALog->GetEntryByIndex((uint32_t)Entry);
        File << stdstr_f("0x%08X,0x%08X,0x%08X\r\n", E->romAddr, E->ramAddr, E->length);
    }
    return true;
}

void DmaLogView::RamAddrChanged()
{
    if (m_ConvertingAddress)
    {
        return;
    }
    QString RomAddrText;
    const uint32_t RamAddr = m_RamEdit->text().toUInt(nullptr, 16);
    uint32_t RomAddr, Offset;
    DMALOGENTRY * Entry = m_DMALog->GetEntryByRamAddress(RamAddr, &RomAddr, &Offset);
    if (Entry != nullptr)
    {
        RomAddrText = QString("%1").arg(RomAddr, 8, 16, QChar('0')).toUpper();
        m_BlockInfo->setText(Tr("Block: %1 -> %2 [%3] +%4").arg(RomAddr, 8, 16, QChar('0')).arg(RamAddr, 8, 16, QChar('0')).arg(Entry->length, 0, 16).arg(Offset, 0, 16).toUpper());
    }
    else
    {
        RomAddrText = "????????";
        m_BlockInfo->setText(Tr("Block: ?"));
    }
    m_ConvertingAddress = true;
    m_RomEdit->setText(RomAddrText);
    m_ConvertingAddress = false;
}

void DmaLogView::RomAddrChanged()
{
    if (m_ConvertingAddress)
    {
        return;
    }
    QString RamAddrText;
    const uint32_t RomAddr = m_RomEdit->text().toUInt(nullptr, 16);
    uint32_t RamAddr, Offset;
    DMALOGENTRY * Entry = m_DMALog->GetEntryByRomAddress(RomAddr, &RamAddr, &Offset);
    if (Entry != nullptr)
    {
        RamAddrText = QString("%1").arg(RamAddr, 8, 16, QChar('0')).toUpper();
        m_BlockInfo->setText(Tr("Block: %1 -> %2 [%3] +%4").arg(RomAddr, 8, 16, QChar('0')).arg(RamAddr, 8, 16, QChar('0')).arg(Entry->length, 0, 16).arg(Offset, 0, 16).toUpper());
    }
    else
    {
        RamAddrText = "????????";
        m_BlockInfo->setText(Tr("Block: ?"));
    }
    m_ConvertingAddress = true;
    m_RamEdit->setText(RamAddrText);
    m_ConvertingAddress = false;
}
