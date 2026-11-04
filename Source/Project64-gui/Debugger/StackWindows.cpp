#include "stdafx.h"
#include "../Vortex/Strings.h"

#include "StackWindows.h"
#include "DebuggerUI.h"

#include <QEvent>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>

using Vortex::Tr;

StackView::StackView(CDebuggerUI * Debugger) :
    DebugWindow(Debugger, DebuggerUI_StackPos, true)
{
    setWindowTitle(Tr("Stack"));
    resize(197 * 1.6, 212 * 1.6);
    QGridLayout * Layout = new QGridLayout(this);
    m_Sp = new QLabel("SP: 00000000", this);
    QPushButton * View = new QPushButton(Tr("View..."), this);
    Layout->addWidget(m_Sp, 0, 0);
    Layout->addWidget(View, 0, 1);
    m_List = new QTableWidget(0, 5, this);
    m_List->setHorizontalHeaderLabels({"#", "00", "04", "08", "0C"});
    m_List->verticalHeader()->hide();
    m_List->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_List->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_List->setColumnWidth(0, 30);
    for (int i = 1; i < 5; i++)
    {
        m_List->setColumnWidth(i, 80);
    }
    Layout->addWidget(m_List, 1, 0, 1, 2);
    Layout->setColumnStretch(0, 1);
    connect(View, &QPushButton::clicked, this, [this]() {
        if (g_Reg != nullptr)
        {
            m_Debugger->Debug_ShowMemoryLocation(g_Reg->m_GPR[29].UW[0], true);
        }
    });
    LoadWindowPos();
}

void StackView::Refresh()
{
    if (g_Reg == nullptr)
    {
        return;
    }
    m_List->setUpdatesEnabled(false);
    m_List->clearContents();
    m_List->setRowCount(0x10);
    const uint32_t SpBase = g_Reg->m_GPR[29].UW[0];
    m_Sp->setText(QString("SP: %1").arg(SpBase, 8, 16, QChar('0')).toUpper());
    for (int i = 0; i < 0x10; i++)
    {
        m_List->setItem(i, 0, new QTableWidgetItem(QString("%1").arg(i * 0x10, 2, 16, QChar('0')).toUpper()));
        for (int j = 0; j < 4; j++)
        {
            const uint32_t VAddr = SpBase + (i * 0x10) + (j * 4);
            uint32_t Value;
            if (!m_Debugger->DebugLoad_VAddr(VAddr, Value))
            {
                m_List->setItem(i, j + 1, new QTableWidgetItem("????????"));
                continue;
            }
            m_List->setItem(i, j + 1, new QTableWidgetItem(QString("%1").arg(Value, 8, 16, QChar('0')).toUpper()));
        }
    }
    m_List->setUpdatesEnabled(true);
}

StackTraceView::StackTraceView(CDebuggerUI * Debugger) :
    DebugWindow(Debugger, DebuggerUI_StackTracePos, true)
{
    setWindowTitle(Tr("Stack Trace"));
    resize(223 * 1.6, 221 * 1.6);
    QGridLayout * Layout = new QGridLayout(this);
    Layout->setContentsMargins(0, 0, 0, 0);
    m_List = new QTableWidget(0, 3, this);
    m_List->setHorizontalHeaderLabels({Tr("Caller"), Tr("Routine"), Tr("Name")});
    m_List->verticalHeader()->hide();
    m_List->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_List->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_List->horizontalHeader()->setStretchLastSection(true);
    m_List->setColumnWidth(0, 90);
    m_List->setColumnWidth(1, 90);
    Layout->addWidget(m_List);
    connect(m_List, &QTableWidget::cellDoubleClicked, this, [this](int Row, int) {
        QTableWidgetItem * Item = m_List->item(Row, 1);
        if (Item != nullptr)
        {
            m_Debugger->Debug_ShowCommandsLocation(Item->data(Qt::UserRole).toUInt(), true);
        }
    });
    LoadWindowPos();
}

// OnActivate
void StackTraceView::changeEvent(QEvent * Event)
{
    DebugWindow::changeEvent(Event);
    if (Event->type() == QEvent::ActivationChange && isActiveWindow())
    {
        Refresh();
    }
}

void StackTraceView::Refresh()
{
    if (!g_DebugSettings.stepping)
    {
        return;
    }
    const int Count = m_EntriesIndex;
    setWindowTitle(Tr("Stack trace (%1)").arg(Count));
    m_List->setUpdatesEnabled(false);
    m_List->clearContents();
    m_List->setRowCount(Count);
    for (int i = 0; i < Count; i++)
    {
        const uint32_t RoutineAddress = m_Entries[i].routineAddress;
        const uint32_t CallingAddress = m_Entries[i].callingAddress;
        m_List->setItem(i, 0, new QTableWidgetItem(QString("%1").arg(CallingAddress, 8, 16, QChar('0')).toUpper()));
        QTableWidgetItem * Routine = new QTableWidgetItem(QString("%1").arg(RoutineAddress, 8, 16, QChar('0')).toUpper());
        Routine->setData(Qt::UserRole, RoutineAddress);
        m_List->setItem(i, 1, Routine);
        CSymbol Symbol;
        m_List->setItem(i, 2, new QTableWidgetItem(m_Debugger->SymbolTable()->GetSymbolByAddress(RoutineAddress, &Symbol) ? QString::fromUtf8(Symbol.m_Name) : QString()));
    }
    m_List->setUpdatesEnabled(true);
}
