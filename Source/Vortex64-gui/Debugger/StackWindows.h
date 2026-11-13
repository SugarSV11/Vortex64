#pragma once

#include "stdafx.h"

#include "DebugWindow.h"

class QLabel;
class QTableWidget;

// Qt port of CDebugStackView (Debugger-StackView.cpp): 16 rows of four words at SP
class StackView : public DebugWindow
{
    Q_OBJECT
public:
    StackView(CDebuggerUI * Debugger);
    void Refresh();
    QTableWidget * List()
    {
        return m_List;
    }
    QLabel * SpLabel()
    {
        return m_Sp;
    }

private:
    QTableWidget * m_List;
    QLabel * m_Sp;
};

#define STACKTRACE_MAX_ENTRIES 100

// Qt port of CDebugStackTrace (Debugger-StackTrace.cpp): the JAL/JALR call stack that
// CDebuggerUI::CPUStepEnded maintains
class StackTraceView : public DebugWindow
{
    Q_OBJECT
public:
    StackTraceView(CDebuggerUI * Debugger);
    void Refresh();

    // Called from the emulation thread (CPUStepEnded); the list is only redrawn in the GUI thread
    inline void PushEntry(uint32_t routineAddress, uint32_t callingAddress)
    {
        if (m_EntriesIndex < STACKTRACE_MAX_ENTRIES)
        {
            m_Entries[m_EntriesIndex] = {routineAddress, callingAddress};
            m_EntriesIndex++;
        }
    }
    inline void PopEntry()
    {
        if (m_EntriesIndex > 0)
        {
            m_EntriesIndex--;
        }
    }
    inline void ClearEntries()
    {
        m_EntriesIndex = 0;
    }
    int Entries() const
    {
        return m_EntriesIndex;
    }
    QTableWidget * List()
    {
        return m_List;
    }

protected:
    void changeEvent(QEvent * Event) override;

private:
    struct Entry
    {
        uint32_t routineAddress;
        uint32_t callingAddress;
    };
    Entry m_Entries[STACKTRACE_MAX_ENTRIES];
    volatile int m_EntriesIndex = 0;
    QTableWidget * m_List;
};
