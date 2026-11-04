#pragma once

#include "SettingsPage.h"
#include "SettingsPages.h"

#include <QVBoxLayout>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QLineEdit;

// The pages of the Vortex64 settings window that are not a port of a Project64 page: the language, the library
// directories, and the rows that show the plugin of each kind (video, audio, input, RSP) where the user looks for
// it. They edit the same settings as the Project64 pages: a plugin row mirrors the combo box of the plugin page.
class QSpinBox;

namespace Vortex
{
// The name that a plugin reports as the user reads it (no placeholder version of a development build, no word "plugin")
QString PluginDisplayName(const QString & Reported);

// A titled block of a settings page
class Section : public QWidget
{
    Q_OBJECT
public:
    explicit Section(const QString & Title, QWidget * Parent = nullptr);
    QVBoxLayout * Body() const
    {
        return m_Body;
    }

private:
    QVBoxLayout * m_Body;
};

// "Vertical synchronization" (Auto, On, Off) of the presentation and the monitor that is detected. The same two plugins follow it
// (Project64-plugin-spec/PresentControl.h); the monitor is information, never something to type.
class VSyncPage : public QtSettingsPage
{
    Q_OBJECT
public:
    explicit VSyncPage(QWidget * Parent = nullptr);
    LanguageStringID PageTitle() override;
    bool EnableReset() override;
    void ResetPage() override;
    void ApplySettings(bool UpdateScreen) override;
    void UpdatePageSettings() override;
    QComboBox * Combo() const
    {
        return m_Mode;
    }
    // "Maximum FPS": the presets, and the box of the custom value (shown only with "Custom...")
    QComboBox * MaxFpsCombo() const
    {
        return m_MaxFps;
    }
    QSpinBox * MaxFpsCustom() const
    {
        return m_MaxFpsCustom;
    }
    // the value that Apply would save: 0 automatic, or 10..1000
    int ChosenMaxFps() const;
    QLabel * DisplayLabel() const
    {
        return m_Display;
    }
    void ShowDisplay();

protected:
    void showEvent(QShowEvent * Event) override;

private:
    void ShowMaxFpsEditor();
    QComboBox * m_Mode;
    QComboBox * m_MaxFps;
    QSpinBox * m_MaxFpsCustom;
    bool m_MaxFpsChanged = false;
    QLabel * m_Display;
    class QTimer * m_Timer;
};

// "Language" of the core (the language files) and of Vortex64
class LanguagePage : public QtSettingsPage
{
    Q_OBJECT
public:
    explicit LanguagePage(QWidget * Parent = nullptr);
    LanguageStringID PageTitle() override;
    void UpdatePageSettings() override;
    void ApplySettings(bool UpdateScreen) override;
    bool EnableReset() override;
    void ResetPage() override;
    QComboBox * Combo() const
    {
        return m_Language;
    }
    QCheckBox * ConfirmExit() const
    {
        return m_ConfirmExit;
    }

private:
    QComboBox * m_Language;
    QCheckBox * m_ConfirmExit;
    QString m_ShownLanguage;
    bool m_ShownConfirm = false;
    bool m_Changed = false;
};

// The folders of the library: RomList_GameDir of the core and the additional ones of Vortex64
class LibraryPage : public QtSettingsPage
{
    Q_OBJECT
public:
    explicit LibraryPage(QWidget * Parent = nullptr);
    LanguageStringID PageTitle() override;
    void UpdatePageSettings() override;
    void ApplySettings(bool UpdateScreen) override;
    bool EnableReset() override;
    void ResetPage() override;
    QLineEdit * RomDirectory() const
    {
        return m_RomDir;
    }
    QListWidget * ExtraList() const
    {
        return m_Extra;
    }
    QCheckBox * Recursive() const
    {
        return m_Recursive;
    }

private:
    QLineEdit * m_RomDir;
    QListWidget * m_Extra;
    QCheckBox * m_Recursive;
    QPushButton * m_Remove;
    bool m_Changed = false;
};

// One plugin kind: the plugin, change it, configure it, about it
class PluginRow : public QWidget
{
    Q_OBJECT
public:
    // Type: PLUGIN_TYPE_VIDEO, ...
    PluginRow(QtPluginPage * Owner, uint16_t Type, const QString & Title, QWidget * Parent = nullptr);
    void Refresh();
    QComboBox * Combo() const
    {
        return m_Combo;
    }
    QPushButton * ConfigureButton() const
    {
        return m_Configure;
    }
    QPushButton * AboutButton() const
    {
        return m_About;
    }

private:
    QtPluginPage * m_Owner;
    uint16_t m_Type;
    QComboBox * m_Combo;
    QPushButton * m_Configure;
    QPushButton * m_About;
    QLabel * m_Info;
};

// A check box that follows another one (of another page) and clicks it
QCheckBox * MirrorCheck(QCheckBox * Owner, QWidget * Parent, const QString & Text = QString());
QCheckBox * FindCheck(QtSettingsPage * Page, SettingID Id);
} // namespace Vortex
