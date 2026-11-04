#pragma once

#include "stdafx.h"

#include <QTabWidget>
#include <functional>
#include <vector>

class CDebuggerUI;
class NumberEdit;
class QLabel;
class QLineEdit;

// Qt counterpart of CRegisterTabs (Debugger-RegisterTabs.cpp): GPR (64 bit, with HI/LO),
// FPR, COP0 and the RCP register groups. Edits write the register on focus loss while
// the CPU is stepping; GPR labels toggle read (click) / write (right click) register
// breakpoints and the current instruction's register usage colours the edits.
class RegisterTabs : public QTabWidget
{
    Q_OBJECT
public:
    RegisterTabs(CDebuggerUI * Debugger, QWidget * Parent = nullptr);

    void RefreshEdits();
    void SetColorsEnabled(bool Enabled);
    void CopyTabRegisters();
    void CopyAllRegisters();

private:
    struct Field
    {
        QString Label;
        QLabel * LabelWidget = nullptr;
        QLineEdit * Edit = nullptr;
        bool Is64 = false;
        std::function<uint32_t()> Get;
        std::function<void(uint32_t)> Set;
    };
    struct Tab
    {
        QString Name;
        QString Address; // shown in the copied text: " (A4040000)"
        std::vector<Field> Fields;
        QLabel * Note = nullptr;
    };

    void BuildTabs();
    QWidget * BuildGrid(Tab & TabData, int Columns);
    void FieldEdited(int TabIndex, int FieldIndex);
    void RefreshColors();
    QString TabText(int Index);
    void ToggleBreakpoint(int FieldIndex, bool Write);

    CDebuggerUI * m_Debugger;
    std::vector<Tab> m_Tabs;
    bool m_ColorsEnabled = false;
    bool m_Refreshing = false;
};
