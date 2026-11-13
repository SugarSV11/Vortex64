#pragma once

#include "stdafx.h"

#include <QMainWindow>

class DebugWindow;
class QTabWidget;

// "Vortex64 - Debugger": one window with a tab per debugger view (CPU commands, memory, search, dump, symbols, stack, trace,
// TLB, exceptions, DMA log, CPU log, scripts). The views are the debugger's own windows (DebugWindow) placed in the tabs
// instead of top-level windows; their behaviour does not change.
class DebuggerShell : public QMainWindow
{
    Q_OBJECT
public:
    static DebuggerShell & Instance();
    // Puts the view in its tab, selects it and shows the window
    void Present(DebugWindow * View);
    // The view is no longer wanted (its game ended): the window stays if other views are
    void Dismiss(DebugWindow * View);
    bool Hosts(const DebugWindow * View) const;
    QTabWidget * Tabs() const
    {
        return m_Tabs;
    }

protected:
    void closeEvent(QCloseEvent * Event) override;
    void moveEvent(QMoveEvent * Event) override;
    void resizeEvent(QResizeEvent * Event) override;

private:
    DebuggerShell();
    void SaveGeometryLater();
    QTabWidget * m_Tabs;
    bool m_Placed = false;
};
