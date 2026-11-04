#pragma once

#include "stdafx.h"

#include "DebugWindow.h"

class NumberEdit;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;

// Qt port of CDumpMemory (Debugger-MemoryDump.cpp): dumps a virtual address range as
// disassembly text (with PC) or raw big endian bytes
class MemoryDumpView : public DebugWindow
{
    Q_OBJECT
public:
    enum DumpFormat
    {
        DisassemblyWithPC,
        RawBigEndian
    };

    MemoryDumpView(CDebuggerUI * Debugger);

    bool DumpMemory(const QString & FileName, DumpFormat Format, uint32_t StartPC, uint32_t EndPC, uint32_t DumpPC);

    NumberEdit * StartAddress()
    {
        return m_StartAddress;
    }
    NumberEdit * EndAddress()
    {
        return m_EndAddress;
    }
    QLineEdit * FileName()
    {
        return m_FileName;
    }
    QComboBox * Format()
    {
        return m_Format;
    }
    QPushButton * DumpButton()
    {
        return m_Dump;
    }
    // Same as the Dump button (returns false if refused)
    bool DoDump();

private:
    void ChooseFile();

    NumberEdit * m_StartAddress;
    NumberEdit * m_EndAddress;
    NumberEdit * m_PC;
    QCheckBox * m_UseAltPC;
    QLineEdit * m_FileName;
    QComboBox * m_Format;
    QPushButton * m_Dump;
    QPushButton * m_Choose;
    QPushButton * m_Close;
};
