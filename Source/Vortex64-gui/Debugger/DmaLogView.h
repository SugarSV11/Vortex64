#pragma once

#include "stdafx.h"

#include "DebugWindow.h"

#include <atomic>

class CDMALog;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

// Qt port of CDebugDMALogView (Debugger-DMALogView.cpp): the cart->RDRAM DMA log with
// the ROM<->RAM address trace and CSV export
class DmaLogView : public DebugWindow
{
    Q_OBJECT
public:
    DmaLogView(CDebuggerUI * Debugger);

    // Called from the emulation thread when a DMA is logged / the log is reset
    void RefreshDMALogWindow(bool Reset = false);
    void RefreshList();
    bool Export(const QString & Path);

    QTableWidget * List()
    {
        return m_List;
    }
    QLineEdit * RamEdit()
    {
        return m_RamEdit;
    }
    QLineEdit * RomEdit()
    {
        return m_RomEdit;
    }
    QLabel * BlockInfo()
    {
        return m_BlockInfo;
    }
    QPushButton * ExportButton()
    {
        return m_ExportButton;
    }
    QPushButton * ClearButton()
    {
        return m_ClearButton;
    }

private:
    void RamAddrChanged();
    void RomAddrChanged();

    CDMALog * m_DMALog;
    QTableWidget * m_List;
    QLineEdit * m_RamEdit;
    QLineEdit * m_RomEdit;
    QLabel * m_BlockInfo;
    QPushButton * m_ExportButton;
    QPushButton * m_ClearButton;
    int m_LastStartIndex = 0;
    bool m_ConvertingAddress = false;
    std::atomic<bool> m_RefreshPending{false};
};
