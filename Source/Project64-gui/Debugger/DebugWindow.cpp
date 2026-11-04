#include "stdafx.h"
#include "DebugWindow.h"
#include "DebuggerShell.h"

#include <QCloseEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <cstdio>
#include <string>

DebugWindow::DebugWindow(CDebuggerUI * Debugger, UISettingID PositionSetting, bool SaveSize, QWidget * Parent) :
    QDialog(Parent, Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint),
    m_Debugger(Debugger),
    m_PositionSetting(PositionSetting),
    m_SaveSize(SaveSize)
{
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    LoadWindowPos();
}

void DebugWindow::ShowWindow()
{
    DebuggerShell::Instance().Present(this);
}

void DebugWindow::HideWindow()
{
    if (m_Embedded)
    {
        DebuggerShell::Instance().Dismiss(this);
    }
    else
    {
        hide();
    }
}

// The view becomes a page of the debugger window: no frame of its own, no position of its own
void DebugWindow::HostInShell()
{
    m_Embedded = true;
    setWindowFlags(Qt::Widget);
}

void DebugWindow::HostedAway()
{
    m_Embedded = false;
    setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    hide();
}

// (Escape would hide the page)
void DebugWindow::reject()
{
    if (!m_Embedded)
    {
        QDialog::reject();
    }
}

void DebugWindow::LoadWindowPos()
{
    m_Loaded = true;
    const std::string Text = UISettingsLoadStringVal(m_PositionSetting);
    int Left, Top, Width, Height;
    const int Params = sscanf(Text.c_str(), "%d,%d,%d,%d", &Left, &Top, &Width, &Height);
    if (Params == 4 && m_SaveSize)
    {
        setGeometry(Left, Top, Width, Height);
    }
    else if (Params >= 2)
    {
        move(Left, Top);
    }
}

void DebugWindow::SaveWindowPos()
{
    if (m_Embedded || !m_Loaded || !isVisible())
    {
        return;
    }
    const QRect Rect = geometry();
    if (m_SaveSize)
    {
        UISettingsSaveString(m_PositionSetting, QString("%1,%2,%3,%4").arg(Rect.left()).arg(Rect.top()).arg(Rect.width()).arg(Rect.height()).toStdString());
    }
    else
    {
        UISettingsSaveString(m_PositionSetting, QString("%1,%2").arg(Rect.left()).arg(Rect.top()).toStdString());
    }
}

void DebugWindow::moveEvent(QMoveEvent * Event)
{
    QDialog::moveEvent(Event);
    SaveWindowPos();
}

void DebugWindow::resizeEvent(QResizeEvent * Event)
{
    QDialog::resizeEvent(Event);
    SaveWindowPos();
}

void DebugWindow::closeEvent(QCloseEvent * Event)
{
    SaveWindowPos();
    QDialog::closeEvent(Event);
}
