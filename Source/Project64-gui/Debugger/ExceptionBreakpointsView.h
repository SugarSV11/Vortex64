#pragma once

#include "stdafx.h"

#include "DebugWindow.h"

#include <QCheckBox>
#include <vector>

// Qt port of CDebugExcBreakpoints (Debugger-ExceptionBreakpoints.cpp): checkboxes bound to
// the Debugger_ExceptionBreakpoints / FpExceptionBreakpoints / IntrBreakpoints /
// RcpIntrBreakpoints settings
class ExceptionBreakpointsView : public DebugWindow
{
    Q_OBJECT
public:
    ExceptionBreakpointsView(CDebuggerUI * Debugger);

    struct Meta
    {
        QCheckBox * box;
        uint32_t exc; // exception number (shifted) or bit mask
    };
    // Test hook: the checkbox of a setting bit
    QCheckBox * Find(SettingID Setting, uint32_t ExcOrMask);

private:
    typedef std::vector<Meta> Map;
    QCheckBox * Add(Map & Group, QWidget * Parent, const char * Text, uint32_t Exc);
    void Init(Map & Group, SettingID Setting, bool Shift);
    void Update(Map & Group, SettingID Setting, QCheckBox * Box, bool Checked, bool Shift);
    void Enable(Map & Group, bool Enable);

    Map m_Exc, m_FpExc, m_Intr, m_RcpIntr;
};
