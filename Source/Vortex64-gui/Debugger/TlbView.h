#pragma once

#include "stdafx.h"

#include "DebugWindow.h"

class QRadioButton;
class QTableWidget;

// Qt port of CDebugTlb (Debugger-TLB.cpp): the 32 TLB entries and the 64 "fast TLB" rules
class TlbView : public DebugWindow
{
    Q_OBJECT
public:
    TlbView(CDebuggerUI * Debugger);
    void RefreshTLBWindow();
    QTableWidget * Entries()
    {
        return m_Entries;
    }
    QTableWidget * Rules()
    {
        return m_Rules;
    }
    QRadioButton * EntriesRadio()
    {
        return m_EntriesRadio;
    }
    QRadioButton * RulesRadio()
    {
        return m_RulesRadio;
    }

private:
    QTableWidget * m_Entries;
    QTableWidget * m_Rules;
    QRadioButton * m_EntriesRadio;
    QRadioButton * m_RulesRadio;
};
