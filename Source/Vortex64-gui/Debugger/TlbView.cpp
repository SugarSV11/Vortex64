#include "stdafx.h"
#include "../Vortex/Strings.h"

#include "TlbView.h"
#include "DebuggerUI.h"

#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QRadioButton>
#include <QTableWidget>

using Vortex::Tr;

TlbView::TlbView(CDebuggerUI * Debugger) :
    DebugWindow(Debugger, DebuggerUI_TLBPos, false)
{
    setWindowTitle("TLB");
    resize(290 * 1.8, 326 * 1.6);
    QGridLayout * Layout = new QGridLayout(this);
    QHBoxLayout * Top = new QHBoxLayout;
    Top->addWidget(new QLabel(Tr("View:"), this));
    m_EntriesRadio = new QRadioButton(Tr("TLB Entries"), this);
    m_RulesRadio = new QRadioButton(Tr("TLB Rules"), this);
    m_EntriesRadio->setChecked(true);
    Top->addWidget(m_EntriesRadio);
    Top->addWidget(m_RulesRadio);
    Top->addStretch();
    Layout->addLayout(Top, 0, 0);

    auto MakeTable = [this](int Columns, const QStringList & Headers) {
        QTableWidget * Table = new QTableWidget(0, Columns, this);
        Table->setHorizontalHeaderLabels(Headers);
        Table->verticalHeader()->hide();
        Table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        Table->setSelectionBehavior(QAbstractItemView::SelectRows);
        Table->horizontalHeader()->setStretchLastSection(true);
        return Table;
    };
    m_Entries = MakeTable(5, {Tr("Index"), Tr("Page mask"), Tr("Entry Hi"), Tr("Entry Lo0"), Tr("Entry Lo1")});
    m_Entries->setColumnWidth(0, 60);
    for (int i = 1; i < 5; i++)
    {
        m_Entries->setColumnWidth(i, 130);
    }
    m_Rules = MakeTable(4, {Tr("Index"), Tr("Valid"), Tr("Dirty"), Tr("Rule")});
    m_Rules->setColumnWidth(0, 60);
    m_Rules->setColumnWidth(1, 60);
    m_Rules->setColumnWidth(2, 60);
    m_Rules->hide();
    Layout->addWidget(m_Entries, 1, 0);
    Layout->addWidget(m_Rules, 1, 0);

    connect(m_EntriesRadio, &QRadioButton::toggled, this, [this](bool On) {
        m_Entries->setVisible(On);
        m_Rules->setVisible(!On);
    });
    RefreshTLBWindow();
    LoadWindowPos();
}

void TlbView::RefreshTLBWindow()
{
    if (g_System == nullptr)
    {
        return;
    }
    const QString Undefined("................");
    auto Set = [](QTableWidget * Table, int Row, int Col, const QString & Text) {
        QTableWidgetItem * Item = Table->item(Row, Col);
        if (Item == nullptr)
        {
            Table->setItem(Row, Col, new QTableWidgetItem(Text));
        }
        else if (Item->text() != Text)
        {
            Item->setText(Text);
        }
    };
    auto Hex = [](uint64_t Value) { return QString("0x%1").arg(Value, 0, 16).toUpper().replace("0X", "0x"); };

    TLB_ENTRY * Tlb = g_System->TLB().m_tlb;
    m_Entries->setRowCount(32);
    for (int Count = 0; Count < 32; Count++)
    {
        Set(m_Entries, Count, 0, QString("0x%1").arg(Count, 2, 16, QChar('0')).toUpper().replace("0X", "0x"));
        const bool Defined = Tlb[Count].EntryDefined;
        Set(m_Entries, Count, 1, Defined ? Hex(Tlb[Count].PageMask.Value) : Undefined);
        Set(m_Entries, Count, 2, Defined ? Hex(Tlb[Count].EntryHi.Value) : Undefined);
        Set(m_Entries, Count, 3, Defined ? Hex(Tlb[Count].EntryLo0.Value) : Undefined);
        Set(m_Entries, Count, 4, Defined ? Hex(Tlb[Count].EntryLo1.Value) : Undefined);
    }

    CTLB::FASTTLB * FastTlb = g_System->TLB().m_FastTlb;
    m_Rules->setRowCount(64);
    for (int Count = 0; Count < 64; Count++)
    {
        Set(m_Rules, Count, 0, QString("0x%1").arg(Count, 2, 16, QChar('0')).toUpper().replace("0X", "0x"));
        const bool Valid = FastTlb[Count].ValidEntry;
        Set(m_Rules, Count, 1, Valid ? (FastTlb[Count].VALID ? Tr("Yes") : Tr("No")) : Undefined);
        Set(m_Rules, Count, 2, Valid && FastTlb[Count].VALID ? (FastTlb[Count].DIRTY ? Tr("Yes") : Tr("No")) : Undefined);
        Set(m_Rules, Count, 3,
            Valid && FastTlb[Count].VALID ? QString("%1:%2 -> %3:%4")
                                                .arg(FastTlb[Count].VSTART, 8, 16, QChar('0'))
                                                .arg(FastTlb[Count].VEND, 8, 16, QChar('0'))
                                                .arg(FastTlb[Count].PHYSSTART, 8, 16, QChar('0'))
                                                .arg(FastTlb[Count].PHYSEND, 8, 16, QChar('0'))
                                                .toUpper()
                                          : Undefined);
    }
}
