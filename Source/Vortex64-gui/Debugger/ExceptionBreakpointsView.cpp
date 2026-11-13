#include "stdafx.h"
#include "../Vortex/Strings.h"

#include "ExceptionBreakpointsView.h"
#include "DebuggerUI.h"

#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>

using Vortex::Tr;

ExceptionBreakpointsView::ExceptionBreakpointsView(CDebuggerUI * Debugger) :
    DebugWindow(Debugger, DebuggerUI_ExceptionBPPos, false)
{
    setWindowTitle(Tr("CPU Exception Breakpoints"));
    QGridLayout * Layout = new QGridLayout(this);

    QGroupBox * ExcGroup = new QGroupBox(Tr("Exceptions"), this);
    QGridLayout * ExcLayout = new QGridLayout(ExcGroup);
    static const struct
    {
        const char * text;
        uint32_t exc;
    } Left[] = {{"Interrupt", 0}, {"TLB Mod", 1}, {"Read TLB Miss", 2}, {"Write TLB Miss", 3}, {"Read Address Error", 4}, {"Write Address Error", 5}, {"Instruction Bus Error", 6}, {"Data Bus Error", 7}, {"SYSCALL", 8}},
        Right[] = {{"BREAK", 9}, {"Illegal Instruction", 10}, {"Coprocessor Unusable", 11}, {"Overflow", 12}, {"Trap exception", 13}, {"Virtual Coherency on Instruction Fetch", 14}, {"Floating Point Exception", 15}, {"Watchpoint Reference", 23}, {"Virtual Coherency on Data Read", 31}};
    for (int i = 0; i < 9; i++)
    {
        ExcLayout->addWidget(Add(m_Exc, ExcGroup, Left[i].text, Left[i].exc), i, 0);
        ExcLayout->addWidget(Add(m_Exc, ExcGroup, Right[i].text, Right[i].exc), i, 1);
    }
    Layout->addWidget(ExcGroup, 0, 0, 1, 3);

    QGroupBox * FpGroup = new QGroupBox(Tr("Floating Point Exceptions"), this);
    QGridLayout * FpLayout = new QGridLayout(FpGroup);
    static const struct
    {
        const char * text;
        uint32_t mask;
    } Fp[] = {{"Inexact Operation", 1 << 0}, {"Underflow", 1 << 1}, {"Overflow", 1 << 2}, {"Division by Zero", 1 << 3}, {"Invalid Operation", 1 << 4}, {"Unimplemented Operation", 1 << 5}};
    for (int i = 0; i < 6; i++)
    {
        FpLayout->addWidget(Add(m_FpExc, FpGroup, Fp[i].text, Fp[i].mask), i, 0);
    }
    Layout->addWidget(FpGroup, 1, 0);

    QGroupBox * IntrGroup = new QGroupBox(Tr("Interrupts"), this);
    QGridLayout * IntrLayout = new QGridLayout(IntrGroup);
    static const char * const Intr[] = {"IP0 Software", "IP1 Software", "IP2 RCP", "IP3 Cartridge", "IP4 Pre-NMI", "IP5 RDB Read", "IP6 RDB Write", "IP7 Timer"};
    for (int i = 0; i < 8; i++)
    {
        IntrLayout->addWidget(Add(m_Intr, IntrGroup, Intr[i], 1 << i), i, 0);
    }
    Layout->addWidget(IntrGroup, 1, 1);

    QGroupBox * RcpGroup = new QGroupBox(Tr("IP2 Interrupts"), this);
    QGridLayout * RcpLayout = new QGridLayout(RcpGroup);
    static const char * const Rcp[] = {"SP Interrupt", "SI Interrupt", "AI Interrupt", "VI Interrupt", "PI Interrupt", "DP Interrupt"};
    for (int i = 0; i < 6; i++)
    {
        RcpLayout->addWidget(Add(m_RcpIntr, RcpGroup, Rcp[i], 1 << i), i, 0);
    }
    Layout->addWidget(RcpGroup, 1, 2);

    Init(m_Exc, Debugger_ExceptionBreakpoints, true);
    Init(m_FpExc, Debugger_FpExceptionBreakpoints, false);
    Init(m_Intr, Debugger_IntrBreakpoints, false);
    Init(m_RcpIntr, Debugger_RcpIntrBreakpoints, false);

    const uint32_t ExcBreakpoints = g_Settings->LoadDword(Debugger_ExceptionBreakpoints);
    const uint32_t IntrBreakpoints = g_Settings->LoadDword(Debugger_IntrBreakpoints);
    const bool IntrEnabled = (ExcBreakpoints & 0x01) != 0;
    const bool RcpIntrEnabled = (IntrBreakpoints & 0x04) != 0;
    const bool FpExcEnabled = (ExcBreakpoints & (1 << 15)) != 0;
    Enable(m_Intr, IntrEnabled);
    Enable(m_RcpIntr, IntrEnabled && RcpIntrEnabled);
    Enable(m_FpExc, FpExcEnabled);

    struct GroupRef
    {
        Map * group;
        SettingID setting;
        bool shift;
    };
    const GroupRef Groups[] = {{&m_Exc, Debugger_ExceptionBreakpoints, true}, {&m_FpExc, Debugger_FpExceptionBreakpoints, false}, {&m_Intr, Debugger_IntrBreakpoints, false}, {&m_RcpIntr, Debugger_RcpIntrBreakpoints, false}};
    for (const GroupRef & Group : Groups)
    {
        for (const Meta & Entry : *Group.group)
        {
            QCheckBox * Box = Entry.box;
            const uint32_t Exc = Entry.exc;
            const GroupRef Ref = Group;
            connect(Box, &QCheckBox::clicked, this, [this, Box, Exc, Ref](bool Checked) {
                // OnClicked
                if (Ref.group == &m_Exc && Exc == 0)
                {
                    Enable(m_Intr, Checked);
                    const bool ToggleRcpIntr = Checked && (g_Settings->LoadDword(Debugger_IntrBreakpoints) & 0x04);
                    Enable(m_RcpIntr, ToggleRcpIntr);
                }
                if (Ref.group == &m_Exc && Exc == 15)
                {
                    Enable(m_FpExc, Checked);
                }
                if (Ref.group == &m_Intr && Exc == (1 << 2))
                {
                    Enable(m_RcpIntr, Checked);
                }
                Update(*Ref.group, Ref.setting, Box, Checked, Ref.shift);
            });
        }
    }
    LoadWindowPos();
}

QCheckBox * ExceptionBreakpointsView::Add(Map & Group, QWidget * Parent, const char * Text, uint32_t Exc)
{
    QCheckBox * Box = new QCheckBox(Tr(Text), Parent);
    Group.push_back({Box, Exc});
    return Box;
}

QCheckBox * ExceptionBreakpointsView::Find(SettingID Setting, uint32_t ExcOrMask)
{
    Map * Group = Setting == Debugger_ExceptionBreakpoints ? &m_Exc : (Setting == Debugger_FpExceptionBreakpoints ? &m_FpExc : (Setting == Debugger_IntrBreakpoints ? &m_Intr : &m_RcpIntr));
    for (const Meta & Entry : *Group)
    {
        if (Entry.exc == ExcOrMask)
        {
            return Entry.box;
        }
    }
    return nullptr;
}

void ExceptionBreakpointsView::Init(Map & Group, SettingID Setting, bool Shift)
{
    const uint32_t Bits = g_Settings->LoadDword(Setting);
    for (const Meta & Entry : Group)
    {
        const uint32_t Bit = Shift ? (1 << Entry.exc) : Entry.exc;
        if (Bits & Bit)
        {
            Entry.box->setChecked(true);
        }
    }
}

void ExceptionBreakpointsView::Update(Map & Group, SettingID Setting, QCheckBox * Box, bool Checked, bool Shift)
{
    for (const Meta & Entry : Group)
    {
        if (Entry.box == Box)
        {
            const uint32_t Bit = Shift ? (1 << Entry.exc) : Entry.exc;
            uint32_t Bits = g_Settings->LoadDword(Setting);
            if (Checked)
            {
                Bits |= Bit;
            }
            else
            {
                Bits &= ~Bit;
            }
            g_Settings->SaveDword(Setting, Bits);
            return;
        }
    }
}

void ExceptionBreakpointsView::Enable(Map & Group, bool Enable)
{
    for (const Meta & Entry : Group)
    {
        Entry.box->setEnabled(Enable);
    }
}
