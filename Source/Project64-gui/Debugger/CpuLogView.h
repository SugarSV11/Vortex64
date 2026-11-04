#pragma once

#include "stdafx.h"

#include "DebugWindow.h"

class CCPULog;
class QCheckBox;
class QPlainTextEdit;
class QPushButton;
class QScrollBar;
class QTableWidget;
class NumberEdit;

// Qt port of CDebugCPULogView (Debugger-CPULogView.cpp): the CPU log ring buffer (a copy is
// taken when logging stops) with the register state of the selected instruction
class CpuLogView : public DebugWindow
{
    Q_OBJECT
public:
    CpuLogView(CDebuggerUI * Debugger);
    ~CpuLogView();

    void RefreshList(bool UpdateBuffer = false);
    void ToggleLoggingEnabled();
    bool Export(const QString & Path);

    QTableWidget * List()
    {
        return m_List;
    }
    QPlainTextEdit * StateInfo()
    {
        return m_StateInfo;
    }
    QCheckBox * Enabled()
    {
        return m_Enabled;
    }
    QPushButton * ExportButton()
    {
        return m_Export;
    }
    NumberEdit * BufferSize()
    {
        return m_BufferSize;
    }
    QScrollBar * Scrollbar()
    {
        return m_Scrollbar;
    }

protected:
    void wheelEvent(class QWheelEvent * Event) override;
    void resizeEvent(class QResizeEvent * Event) override;
    void showEvent(class QShowEvent * Event) override;

private:
    void ShowRegStates(size_t StateIndex);
    int NumVisibleRows();
    void ScrollTo(int Position);

    CCPULog * m_CPULogCopy = nullptr;
    int m_LogStartIndex = 0;
    QTableWidget * m_List;
    QPlainTextEdit * m_StateInfo;
    QCheckBox * m_Enabled;
    NumberEdit * m_BufferSize;
    QScrollBar * m_Scrollbar;
    QPushButton * m_Export;
};
