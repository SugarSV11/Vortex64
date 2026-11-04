#pragma once

#include "stdafx.h"
#include "MainMenuID.h"
#include <Project64/UserInterface/MenuShortCuts.h>

#include <QObject>
#include <QString>

class MainWindow;
class QAction;
class QMenu;
class QKeySequence;

// Qt counterpart of CMainMenu (Project64/UserInterface/MainMenu.cpp): the same
// menu structure, the same enable/tick rules and the same command handling,
// built from the same language strings and the same CShortCuts key table.
//
// CMainMenu::ResetMenu() is called by the Windows GUI from settings change
// callbacks; here Poll() (a timer) compares the values those menu items depend
// on and calls ResetMenu() when one of them changed.
class QtMainMenu : public QObject
{
    Q_OBJECT
public:
    explicit QtMainMenu(MainWindow * Window);
    ~QtMainMenu() override;

    void ResetMenu(void);
    bool ProcessMessage(int MenuID);

    // Reloads the key table after Settings > Keyboard shortcuts changed it.
    void ShortCutsChanged(void);
    CShortCuts & ShortCuts(void)
    {
        return m_ShortCuts;
    }

    // Qt::Key for a Windows virtual key code (the shortcuts file stores VK codes)
    static int QtKeyFromVirtualKey(int VirtualKey);
    static QKeySequence KeySequenceFor(const CMenuShortCutKey & Key);

private Q_SLOTS: // QT_NO_KEYWORDS is set (see CMakeLists.txt)
    void Poll(void);

private:
    void FillOutMenu(void);
    QString GetSaveSlotString(int Slot);
    QString StateKey(void) const;
    void SetTraceModuleSetttings(SettingID Type);

    void OnOpenRom(void);
    void OnOpenCombo(void);
    void OnEndEmulation(void);
    void OnScreenShot(void);
    void OnSaveAs(void);
    void OnLoadState(void);
    void OnSwapDisk(void);

    MainWindow * m_Window;
    CShortCuts m_ShortCuts;
    QString m_StateKey;
    // Owns every menu and action of the current menu bar; replaced by ResetMenu().
    QObject * m_Holder;
};
