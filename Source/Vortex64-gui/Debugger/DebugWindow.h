#pragma once

#include "stdafx.h"

#include <Vortex64-ui/Settings/UISettings.h>

#include <QDialog>

class CDebuggerUI;

// CDebugDialog counterpart: a non-modal top-level debugger window that remembers its
// position (and size) in a UI setting like upstream ("left,top[,width,height]").
class DebugWindow : public QDialog
{
    Q_OBJECT
public:
    DebugWindow(CDebuggerUI * Debugger, UISettingID PositionSetting, bool SaveSize, QWidget * Parent = nullptr);

    // The debugger views live in the tabs of "Vortex64 - Debugger" (DebuggerShell)
    void ShowWindow();
    void HideWindow();
    void HostInShell();
    void HostedAway();
    bool Embedded() const
    {
        return m_Embedded;
    }

protected:
    void LoadWindowPos();
    void SaveWindowPos();
    void moveEvent(QMoveEvent * Event) override;
    void resizeEvent(QResizeEvent * Event) override;
    void closeEvent(QCloseEvent * Event) override;
    void reject() override;

    CDebuggerUI * m_Debugger;

private:
    UISettingID m_PositionSetting;
    bool m_SaveSize;
    bool m_Loaded = false;
    bool m_Embedded = false;
};
