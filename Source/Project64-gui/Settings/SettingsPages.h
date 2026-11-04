#pragma once

#include "SettingsPage.h"

#include <Common/StdString.h>
#include <Common/path.h>
#include <Project64-plugin-spec/Base.h>

// Qt ports of upstream's Settings pages (Source/Project64/UserInterface/
// Settings/SettingsPage-*.cpp). SettingIDs, language strings, defaults and
// apply/reset semantics are the same as the WTL pages.

#include <Project64/Plugins/PluginList.h>
#include "../RomBrowser.h"

class QCheckBox;
class QComboBox;
class QPushButton;

// SettingsPage-Options.cpp
class QtGeneralOptionsPage : public QtSettingsPage
{
    Q_OBJECT
public:
    explicit QtGeneralOptionsPage(QWidget * parent = nullptr);
    LanguageStringID PageTitle() override;

Q_SIGNALS:
    void BasicModeChanged(bool HideAdvanced);
};

// SettingsPage-AdvancedOptions.cpp
class QtAdvancedOptionsPage : public QtSettingsPage
{
    Q_OBJECT
public:
    explicit QtAdvancedOptionsPage(QWidget * parent = nullptr);
    LanguageStringID PageTitle() override;
};

// SettingsPage-Defaults.cpp
class QtDefaultsOptionsPage : public QtSettingsPage
{
    Q_OBJECT
public:
    explicit QtDefaultsOptionsPage(QWidget * parent = nullptr);
    LanguageStringID PageTitle() override;
};

// SettingsPage-Plugin.cpp / SettingsPage-Game-Plugin.cpp: plugin selection
// (Video/Audio/Controller/RSP). The combo boxes are filled from CPluginList,
// i.e. by scanning the plugin directory for shared objects that export
// GetDllInfo(); nothing is hard-coded. In game mode (CGamePluginPage) there
// is an extra "Default" entry and the per-game Game_EditPlugin_* settings are
// used; choosing "Default" deletes the per-game setting.
class QtPluginPage : public QtSettingsPage
{
    Q_OBJECT
public:
    explicit QtPluginPage(bool GameMode = false, QWidget * parent = nullptr);
    ~QtPluginPage() override;
    LanguageStringID PageTitle() override;
    void UpdatePageSettings() override;
    void ApplySettings(bool UpdateScreen) override;
    bool EnableReset() override;
    void ResetPage() override;

    // For the Vortex64 pages, which show the same choices where they belong (Graphics, Audio, Input, RSP)
    QComboBox * Combo(uint16_t PluginType) const;
    bool PendingChange(uint16_t PluginType) const;
    const CPluginList::PLUGIN * PluginFor(uint16_t PluginType) const;
    const CPluginList & PluginList() const
    {
        return m_PluginList;
    }
    void ShowAboutFor(uint16_t PluginType);
    QCheckBox * HleGfxBox() const
    {
        return m_HleGfx;
    }
    QCheckBox * HleAudioBox() const
    {
        return m_HleAudio;
    }

Q_SIGNALS:
    void PluginChoiceChanged();

private:
    struct Entry
    {
        QComboBox * Combo;
        QPushButton * About;
        SettingID Type;
        Modified State;
        uint16_t PluginType = 0;
    };
    QCheckBox * m_HleGfx = nullptr;
    QCheckBox * m_HleAudio = nullptr;
    void AddPlugins(QComboBox * Combo, QPushButton * About, SettingID Type, uint16_t PluginType);
    void UpdateAboutButton(const Entry & E);
    void ShowAbout(const Entry & E);

    bool m_GameMode;
    CPluginList m_PluginList;
    std::vector<Entry *> m_Entries;
};

class QLineEdit;
class QRadioButton;

// SettingsPage-Directories.cpp: each directory has a "default" radio button,
// a "selected" radio button with an edit box and a browse button, stored in
// Directory_*Selected / Directory_*UseSelected.
class QtDirectoriesPage : public QtSettingsPage
{
    Q_OBJECT
public:
    explicit QtDirectoriesPage(QWidget * parent = nullptr);
    ~QtDirectoriesPage() override;
    LanguageStringID PageTitle() override;
    void UpdatePageSettings() override;
    void ApplySettings(bool UpdateScreen) override;
    bool EnableReset() override;
    void ResetPage() override;

private:
    struct DirRow
    {
        QRadioButton * Default;
        QRadioButton * Selected;
        QLineEdit * Edit;
        SettingID SelectedID;
        SettingID UseSelectedID;
        LanguageStringID SelectTitle;
        Modified EditState;   // the directory text
        Modified DefaultState; // the default/selected choice
    };
    void AddRow(class QVBoxLayout * Layout, LanguageStringID Title, const char * DefaultText, LanguageStringID SelectTitle, SettingID SelectedID, SettingID UseSelectedID);
    void Browse(DirRow & Row);

    std::vector<DirRow *> m_Rows;
};

// SettingsPage-DiskDrive.cpp: 64DD IPL ROM paths and disk save type.
class QtDiskDrivePage : public QtSettingsPage
{
    Q_OBJECT
public:
    explicit QtDiskDrivePage(QWidget * parent = nullptr);
    LanguageStringID PageTitle() override;

private:
    void AddIplRow(class QFormLayout * Form, LanguageStringID Text, SettingID Type);
};

// SettingsPage-Game-General.cpp
class QtGameGeneralPage : public QtSettingsPage
{
    Q_OBJECT
public:
    explicit QtGameGeneralPage(QWidget * parent = nullptr);
    LanguageStringID PageTitle() override;
};

// SettingsPage-Game-Recompiler.cpp
class QtGameRecompilerPage : public QtSettingsPage
{
    Q_OBJECT
public:
    explicit QtGameRecompilerPage(QWidget * parent = nullptr);
    LanguageStringID PageTitle() override;
};

// SettingsPage-Game-DiskDrive.cpp
class QtGameDiskDrivePage : public QtSettingsPage
{
    Q_OBJECT
public:
    explicit QtGameDiskDrivePage(QWidget * parent = nullptr);
    LanguageStringID PageTitle() override;
};

// SettingsPage-Game-Status.cpp: the RDB editor (status and notes of the game)
class QtGameStatusPage : public QtSettingsPage
{
    Q_OBJECT
public:
    explicit QtGameStatusPage(QWidget * parent = nullptr);
    LanguageStringID PageTitle() override;
};

class QListWidget;

// SettingsPage-GameBrowser.cpp ("ROM selection"): enables the ROM browser, its
// directory options and the choice/order of the displayed fields.
class QtGameBrowserPage : public QtSettingsPage
{
    Q_OBJECT
public:
    explicit QtGameBrowserPage(QWidget * parent = nullptr);
    LanguageStringID PageTitle() override;
    void UpdatePageSettings() override;
    void ApplySettings(bool UpdateScreen) override;
    bool EnableReset() override;
    void ResetPage() override;

private:
    void UpdateFieldList(const ROMBROWSER_FIELDS_LIST & Fields);
    void FixCtrlState();
    void AddItemTo(QListWidget * List, int FieldIndex, int Position = -1);

    ROMBROWSER_FIELDS_LIST m_Fields;
    QListWidget * m_Available;
    QListWidget * m_Using;
    QCheckBox * m_UseBrowser;
    std::vector<QWidget *> m_FieldControls;
    bool m_OrderChanged;
    bool m_OrderReset;
};
