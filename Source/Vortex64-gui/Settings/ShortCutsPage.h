#pragma once

#include "SettingsPage.h"
#include "../stdafx.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// Qt counterpart of COptionsShortCutsPage (Project64/UserInterface/Settings/
// SettingsPage-KeyboardShortcuts.cpp): edits a CShortCuts key table per CPU
// state and saves it to Project64.sc3 on Apply.
class QtShortCutsPage : public QtSettingsPage
{
    Q_OBJECT
public:
    explicit QtShortCutsPage(QWidget * parent = nullptr);
    LanguageStringID PageTitle() override;
    void ApplySettings(bool UpdateScreen) override;
    bool EnableReset() override;
    void ResetPage() override;
    void UpdatePageSettings() override;

private Q_SLOTS: // QT_NO_KEYWORDS is set
    void OnCpuStateChanged();
    void OnMenuItemChanged();
    void OnRemoveClicked();
    void OnDetectKeyClicked();
    void OnAssignClicked();
    void OnShortCutChanged();

private:
    typedef CMenuShortCutKey::RUNNING_STATE RUNNING_STATE;

    RUNNING_STATE CurrentRunningState() const;
    void RefreshShortCutOptions(QTreeWidgetItem * Item);
    void CheckResetEnable();
    int CurrentKey() const;

    QComboBox * m_CpuState;
    QComboBox * m_VirtualKeyList;
    QTreeWidget * m_MenuItems;
    QListWidget * m_CurrentKeys;
    QCheckBox * m_Ctrl;
    QCheckBox * m_Alt;
    QCheckBox * m_Shift;
    QLabel * m_Assigned;
    CShortCuts m_ShortCuts;
    bool m_EnableReset;
};
