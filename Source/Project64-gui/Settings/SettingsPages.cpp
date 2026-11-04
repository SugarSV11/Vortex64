#include "SettingsPages.h"
#include "../Vortex/Strings.h"
#include <Project64-core/Settings/SettingType/SettingsType-RomDatabase.h>

#include <Project64-core/N64System/N64Types.h>
#include <Project64-core/Notification.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Settings.h>
#include <Project64-core/Settings/DebugSettings.h>

#include <Common/DynamicLibrary.h>
#include <Common/IniFile.h>
#include <Project64-core/Notification.h>
#include <Project64-plugin-spec/Base.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QFileDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QRadioButton>
#include <QPushButton>
#include <QVBoxLayout>

namespace
{
QCheckBox * NewCheck(QWidget * Parent, QVBoxLayout * Layout, LanguageStringID Text)
{
    QCheckBox * CheckBox = new QCheckBox(QtGS(Text), Parent);
    Layout->addWidget(CheckBox);
    return CheckBox;
}

void AddItem(QComboBox * ComboBox, LanguageStringID Text, uint32_t Value)
{
    ComboBox->addItem(QtGS(Text), QVariant::fromValue<uint>(Value));
}
} // namespace

// ---------------------------------------------------------------- Options

QtGeneralOptionsPage::QtGeneralOptionsPage(QWidget * parent) :
    QtSettingsPage(parent)
{
    QVBoxLayout * Layout = new QVBoxLayout(this);

    AddModCheckBox(NewCheck(this, Layout, OPTION_AUTO_SLEEP), (SettingID)Setting_AutoSleep);
    AddModCheckBox(NewCheck(this, Layout, OPTION_AUTO_FULLSCREEN), (SettingID)Setting_AutoFullscreen);
    AddModCheckBox(NewCheck(this, Layout, OPTION_DISABLE_SS), (SettingID)Setting_DisableScrSaver);
    AddModCheckBox(NewCheck(this, Layout, OPTION_DISCORD_RPC), (SettingID)Setting_EnableDiscordRPC);
    QCheckBox * BasicMode = NewCheck(this, Layout, OPTION_BASIC_MODE);
    AddModCheckBox(BasicMode, UserInterface_BasicMode);
    // CGeneralOptionsPage::OnBasicMode: the pages of the advanced settings appear or go at once
    connect(BasicMode, &QCheckBox::clicked, this, [this](bool Hide) { Q_EMIT BasicModeChanged(Hide); });

    QFrame * Line = new QFrame(this);
    Line->setFrameShape(QFrame::HLine);
    Line->setFrameShadow(QFrame::Sunken);
    Layout->addWidget(Line);

    QGridLayout * Grid = new QGridLayout;
    QLabel * MaxRomsText = new QLabel(QtGS(RB_MAX_ROMS), this);
    QLineEdit * Remember = new QLineEdit(this);
    Remember->setMaximumWidth(60);
    Grid->addWidget(MaxRomsText, 0, 0);
    Grid->addWidget(Remember, 0, 1);
    Grid->addWidget(new QLabel(QtGS(RB_ROMS), this), 0, 2);

    QLabel * MaxDirsText = new QLabel(QtGS(RB_MAX_DIRS), this);
    QLineEdit * RememberDir = new QLineEdit(this);
    RememberDir->setMaximumWidth(60);
    Grid->addWidget(MaxDirsText, 1, 0);
    Grid->addWidget(RememberDir, 1, 1);
    Grid->addWidget(new QLabel(QtGS(RB_DIRS), this), 1, 2);
    Grid->setColumnStretch(0, 1);
    Layout->addLayout(Grid);
    Layout->addStretch(1);

    Modified * TxtBox = AddModTextBox(Remember, (SettingID)File_RecentGameFileCount, false);
    TxtBox->TextField = MaxRomsText;
    TxtBox = AddModTextBox(RememberDir, (SettingID)Directory_RecentGameDirCount, false);
    TxtBox->TextField = MaxDirsText;

    UpdatePageSettings();
}

LanguageStringID QtGeneralOptionsPage::PageTitle()
{
    return TAB_OPTIONS;
}

// --------------------------------------------------------------- Advanced

QtAdvancedOptionsPage::QtAdvancedOptionsPage(QWidget * parent) :
    QtSettingsPage(parent)
{
    QVBoxLayout * Layout = new QVBoxLayout(this);

    AddModCheckBox(NewCheck(this, Layout, ADVANCE_AUTO_START), Setting_AutoStart);
    AddModCheckBox(NewCheck(this, Layout, ADVANCE_COMPRESS), Setting_AutoZipInstantSave);
    AddModCheckBox(NewCheck(this, Layout, ADVANCE_DEBUGGER), Debugger_Enabled);
    AddModCheckBox(NewCheck(this, Layout, ADVANCE_ALWAYS_INTERPRETER), Setting_ForceInterpreterCPU);
    AddModCheckBox(NewCheck(this, Layout, OPTION_REMEMBER_CHEAT), Setting_RememberCheats);
    AddModCheckBox(NewCheck(this, Layout, OPTION_CHECK_RUNNING), Setting_CheckEmuRunning);
    AddModCheckBox(NewCheck(this, Layout, OPTION_ENABLE_ENHANCEMENTS), Setting_Enhancement);
    AddModCheckBox(NewCheck(this, Layout, OPTION_UNIQUE_SAVE_DIR), Setting_UniqueSaveDir);

    QHBoxLayout * FrameRow = new QHBoxLayout;
    QCheckBox * FrameRate = new QCheckBox(QtGS(OPTION_CHANGE_FR), this);
    QComboBox * FrameType = new QComboBox(this);
    FrameRow->addWidget(FrameRate);
    FrameRow->addWidget(FrameType, 1);
    Layout->addLayout(FrameRow);
    AddModCheckBox(FrameRate, UserInterface_DisplayFrameRate);

    AddModCheckBox(NewCheck(this, Layout, OPTION_SHOW_STATUS_BAR), (SettingID)UserInterface_ShowStatusBar);
    AddModCheckBox(NewCheck(this, Layout, OPTION_EXIT_FULLSCREEN_ON_LOSE_FOCUS), (SettingID)UserInterface_ExitFullscreenOnLoseFocus);
    Layout->addStretch(1);

    if (AddModComboBox(FrameType, UserInterface_FrameDisplayType) != nullptr)
    {
        AddItem(FrameType, STR_FR_VIS, FR_VIs);
        AddItem(FrameType, STR_FR_DLS, FR_DLs);
        AddItem(FrameType, STR_FR_PERCENT, FR_PERCENT);
        AddItem(FrameType, STR_FR_DLS_VIS, FR_VIs_DLs);
    }

    UpdatePageSettings();
}

LanguageStringID QtAdvancedOptionsPage::PageTitle()
{
    return TAB_ADVANCED;
}

// --------------------------------------------------------------- Defaults

QtDefaultsOptionsPage::QtDefaultsOptionsPage(QWidget * parent) :
    QtSettingsPage(parent)
{
    QVBoxLayout * Layout = new QVBoxLayout(this);
    QFormLayout * Form = new QFormLayout;
    Layout->addLayout(Form);

    // Upstream binds the "Known" combo box to Default_RDRamSizeUnknown and the
    // "Unknown" one to Default_RDRamSizeKnown (SettingsPage-Defaults.cpp); the
    // port keeps that pairing so both frontends read and write the same keys.
    QLabel * KnownText = new QLabel(QtGS(ROM_MEM_SIZE_KNOWN), this);
    QComboBox * SizeKnown = new QComboBox(this);
    Form->addRow(KnownText, SizeKnown);
    QLabel * UnknownText = new QLabel(QtGS(ROM_MEM_SIZE_UNKNOWN), this);
    QComboBox * SizeUnknown = new QComboBox(this);
    Form->addRow(UnknownText, SizeUnknown);
    QLabel * CounterText = new QLabel(QtGS(ROM_COUNTER_FACTOR), this);
    QComboBox * CounterFactor = new QComboBox(this);
    Form->addRow(CounterText, CounterFactor);
    QLabel * ViText = new QLabel(QtGS(ROM_VIREFRESH), this);
    QLineEdit * ViRefresh = new QLineEdit(this);
    Form->addRow(ViText, ViRefresh);
    QLabel * AiText = new QLabel(QtGS(ROM_COUNTPERBYTE), this);
    QLineEdit * AiCount = new QLineEdit(this);
    Form->addRow(AiText, AiCount);
    QLabel * DiskText = new QLabel(QtGS(ROM_DISK_SEEK_TIMING), this);
    QComboBox * DiskSeek = new QComboBox(this);
    Form->addRow(DiskText, DiskSeek);
    QLabel * OverclockText = new QLabel(QtGS(ROM_OVER_CLOCK_MODIFIER), this);
    QLineEdit * Overclock = new QLineEdit(this);
    Form->addRow(OverclockText, Overclock);

    QCheckBox * FixedAudio = new QCheckBox(QtGS(ROM_FIXED_AUDIO), this);
    QCheckBox * SyncAudio = new QCheckBox(QtGS(ROM_SYNC_AUDIO), this);
    QCheckBox * HleGfx = new QCheckBox(QtGS(PLUG_HLE_GFX), this);
    QCheckBox * Randomize = new QCheckBox(QtGS(ROM_RANDOMIZE_SIPI_INTERRUPTS), this);
    QGridLayout * Checks = new QGridLayout;
    Checks->addWidget(FixedAudio, 0, 0);
    Checks->addWidget(SyncAudio, 0, 1);
    Checks->addWidget(HleGfx, 1, 0);
    Checks->addWidget(Randomize, 1, 1);
    Layout->addLayout(Checks);
    Layout->addStretch(1);

    Modified * Item = AddModComboBox(SizeKnown, Default_RDRamSizeUnknown);
    if (Item != nullptr)
    {
        Item->TextField = KnownText;
        AddItem(SizeKnown, RDRAM_4MB, 0x400000);
        AddItem(SizeKnown, RDRAM_8MB, 0x800000);
    }
    Item = AddModComboBox(SizeUnknown, Default_RDRamSizeKnown);
    if (Item != nullptr)
    {
        Item->TextField = UnknownText;
        AddItem(SizeUnknown, RDRAM_4MB, 0x400000);
        AddItem(SizeUnknown, RDRAM_8MB, 0x800000);
    }
    Item = AddModComboBox(CounterFactor, Default_CounterFactor);
    if (Item != nullptr)
    {
        Item->TextField = CounterText;
        AddItem(CounterFactor, NUMBER_1, 1);
        AddItem(CounterFactor, NUMBER_2, 2);
        AddItem(CounterFactor, NUMBER_3, 3);
        AddItem(CounterFactor, NUMBER_4, 4);
        AddItem(CounterFactor, NUMBER_5, 5);
        AddItem(CounterFactor, NUMBER_6, 6);
    }

    AddModTextBox(ViRefresh, Default_ViRefreshRate, false)->TextField = ViText;
    AddModTextBox(AiCount, Default_AiCountPerBytes, false)->TextField = AiText;
    AddModTextBox(Overclock, Default_OverClockModifier, false)->TextField = OverclockText;

    AddModCheckBox(HleGfx, Default_UseHleGfx);
    AddModCheckBox(SyncAudio, Default_SyncViaAudio);
    AddModCheckBox(FixedAudio, Default_FixedAudio);
    AddModCheckBox(Randomize, Default_RandomizeSIPIInterrupts);

    Item = AddModComboBox(DiskSeek, Default_DiskSeekTiming);
    if (Item != nullptr)
    {
        Item->TextField = DiskText;
        AddItem(DiskSeek, ROM_DISK_SEEK_TIMING_TURBO, DiskSeek_Turbo);
        AddItem(DiskSeek, ROM_DISK_SEEK_TIMING_SLOW, DiskSeek_Slow);
    }

    if (!g_Settings->LoadBool(Setting_SyncViaAudioEnabled))
    {
        SyncAudio->setEnabled(false);
    }
    UpdatePageSettings();
}

LanguageStringID QtDefaultsOptionsPage::PageTitle()
{
    return TAB_DEFAULTS;
}

// ----------------------------------------------------------------- Plugins

// the widgets belong to their Qt parents; the entries only point at them
QtPluginPage::~QtPluginPage()
{
    qDeleteAll(m_Entries);
}

QtPluginPage::QtPluginPage(bool GameMode, QWidget * parent) :
    QtSettingsPage(parent),
    m_GameMode(GameMode)
{
    QVBoxLayout * Layout = new QVBoxLayout(this);

    struct Row
    {
        LanguageStringID Title;
        SettingID Type;
        SettingID GameType;
        uint16_t PluginType;
    };
    const Row Rows[] = {
        {PLUG_GFX, Plugin_GFX_Current, Game_EditPlugin_Gfx, PLUGIN_TYPE_VIDEO},
        {PLUG_AUDIO, Plugin_AUDIO_Current, Game_EditPlugin_Audio, PLUGIN_TYPE_AUDIO},
        {PLUG_CTRL, Plugin_CONT_Current, Game_EditPlugin_Contr, PLUGIN_TYPE_CONTROLLER},
        {PLUG_RSP, Plugin_RSP_Current, Game_EditPlugin_RSP, PLUGIN_TYPE_RSP},
    };
    for (const Row & R : Rows)
    {
        QGroupBox * Group = new QGroupBox(QtGS(R.Title), this);
        QHBoxLayout * Box = new QHBoxLayout(Group);
        QComboBox * Combo = new QComboBox(Group);
        QPushButton * About = new QPushButton(QtGS(PLUG_ABOUT), Group);
        Box->addWidget(Combo, 1);
        Box->addWidget(About);
        Layout->addWidget(Group);
        AddPlugins(Combo, About, GameMode ? R.GameType : R.Type, R.PluginType);
    }

    QCheckBox * HleGfx = new QCheckBox(QtGS(PLUG_HLE_GFX), this);
    QCheckBox * HleAudio = new QCheckBox(QtGS(PLUG_HLE_AUDIO), this);
    m_HleGfx = HleGfx;
    m_HleAudio = HleAudio;
    Layout->addWidget(HleGfx);
    Layout->addWidget(HleAudio);
    Layout->addStretch(1);
    AddModCheckBox(HleGfx, GameMode ? Game_UseHleGfx : Plugin_UseHleGfx, [](bool Checked) {
        return Checked || g_Notify->AskYesNoQuestion(GS(MSG_SET_LLE_GFX_MSG));
    });
    AddModCheckBox(HleAudio, GameMode ? Game_UseHleAudio : Plugin_UseHleAudio, [](bool Checked) {
        return !Checked || g_Notify->AskYesNoQuestion(GS(MSG_SET_HLE_AUD_MSG));
    });

    UpdatePageSettings();
}

LanguageStringID QtPluginPage::PageTitle()
{
    return TAB_PLUGIN;
}

void QtPluginPage::AddPlugins(QComboBox * Combo, QPushButton * About, SettingID Type, uint16_t PluginType)
{
    if (m_GameMode)
    {
        Combo->addItem(QtGS(PLUG_DEFAULT), QString());
    }
    for (int i = 0, n = m_PluginList.GetPluginCount(); i < n; i++)
    {
        const CPluginList::PLUGIN * Plugin = m_PluginList.GetPluginInfo(i);
        if (Plugin == nullptr || Plugin->Info.Type != PluginType)
        {
            continue;
        }
        Combo->addItem(QString::fromUtf8(Plugin->Info.Name), QString::fromStdString(Plugin->FileName));
    }
    Entry * E = new Entry{Combo, About, Type, Modified(), PluginType};
    E->State.Widget = Combo;
    m_Entries.push_back(E);
    connect(Combo, QOverload<int>::of(&QComboBox::activated), this, [this, E]() {
        MarkChanged(E->State);
        Q_EMIT PluginChoiceChanged();
    });
    connect(Combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, E]() { UpdateAboutButton(*E); });
    connect(About, &QPushButton::clicked, this, [this, E]() { ShowAbout(*E); });
}

// The plugin behind the current selection of a combo box, or nullptr.
static const CPluginList::PLUGIN * SelectedPlugin(const CPluginList & List, const QComboBox * Combo)
{
    if (Combo->currentIndex() < 0)
    {
        return nullptr;
    }
    const std::string FileName = Combo->currentData().toString().toStdString();
    if (FileName.empty())
    {
        return nullptr;
    }
    for (int i = 0, n = List.GetPluginCount(); i < n; i++)
    {
        const CPluginList::PLUGIN * Plugin = List.GetPluginInfo(i);
        if (Plugin != nullptr && strcasecmp(Plugin->FileName.c_str(), FileName.c_str()) == 0)
        {
            return Plugin;
        }
    }
    return nullptr;
}

QComboBox * QtPluginPage::Combo(uint16_t PluginType) const
{
    for (const Entry * E : m_Entries)
    {
        if (E->PluginType == PluginType)
        {
            return E->Combo;
        }
    }
    return nullptr;
}

bool QtPluginPage::PendingChange(uint16_t PluginType) const
{
    for (const Entry * E : m_Entries)
    {
        if (E->PluginType == PluginType)
        {
            return E->State.Changed || E->State.Reset;
        }
    }
    return false;
}

const CPluginList::PLUGIN * QtPluginPage::PluginFor(uint16_t PluginType) const
{
    QComboBox * C = Combo(PluginType);
    return C != nullptr ? ::SelectedPlugin(m_PluginList, C) : nullptr;
}

void QtPluginPage::ShowAboutFor(uint16_t PluginType)
{
    for (Entry * E : m_Entries)
    {
        if (E->PluginType == PluginType)
        {
            ShowAbout(*E);
        }
    }
}

void QtPluginPage::UpdateAboutButton(const Entry & E)
{
    const CPluginList::PLUGIN * Plugin = SelectedPlugin(m_PluginList, E.Combo);
    E.About->setEnabled(Plugin != nullptr && Plugin->AboutFunction);
}

void QtPluginPage::ShowAbout(const Entry & E)
{
    const CPluginList::PLUGIN * Plugin = SelectedPlugin(m_PluginList, E.Combo);
    if (Plugin == nullptr)
    {
        return;
    }
    DynLibHandle Lib = DynamicLibraryOpen((const char *)Plugin->FullPath, false);
    if (Lib == nullptr)
    {
        return;
    }
    void (*DllAbout)(void * hWnd) = (void (*)(void *))DynamicLibraryGetProc(Lib, "DllAbout");
    if (DllAbout != nullptr)
    {
        DllAbout(reinterpret_cast<void *>(window()->winId()));
    }
    DynamicLibraryClose(Lib);
}

void QtPluginPage::UpdatePageSettings()
{
    QtSettingsPage::UpdatePageSettings();
    for (Entry * E : m_Entries)
    {
        std::string Value;
        bool Stored = g_Settings->LoadStringVal(E->Type, Value);
        SetChanged(E->State, Stored);
        // Game mode: an unset per-game plugin selects the "Default" entry.
        int Index = E->Combo->findData(Stored || !m_GameMode ? QString::fromStdString(Value) : QString(), Qt::UserRole, Qt::MatchFixedString);
        if (Index >= 0)
        {
            E->Combo->setCurrentIndex(Index);
        }
        UpdateAboutButton(*E);
    }
    Q_EMIT PluginChoiceChanged();
}

void QtPluginPage::ApplySettings(bool UpdateScreen)
{
    for (Entry * E : m_Entries)
    {
        if (E->State.Changed && E->Combo->currentIndex() >= 0)
        {
            const std::string FileName = E->Combo->currentData().toString().toStdString();
            if (FileName.empty())
            {
                g_Settings->DeleteSetting(E->Type);
            }
            else if (g_Settings->LoadStringVal(E->Type) != FileName)
            {
                g_Settings->SaveString(E->Type, FileName);
            }
        }
        if (E->State.Reset)
        {
            g_Settings->DeleteSetting(E->Type);
            E->State.Reset = false;
        }
    }
    QtSettingsPage::ApplySettings(UpdateScreen);
}

bool QtPluginPage::EnableReset()
{
    for (const Entry * E : m_Entries)
    {
        if (E->State.Changed)
        {
            return true;
        }
    }
    return QtSettingsPage::EnableReset();
}

void QtPluginPage::ResetPage()
{
    bool Changed = false;
    for (Entry * E : m_Entries)
    {
        if (!E->State.Changed)
        {
            continue;
        }
        SetReset(E->State, true);
        const QString Default = m_GameMode ? QString() : QString::fromStdString(g_Settings->LoadDefaultString(E->Type));
        int Index = E->Combo->findData(Default, Qt::UserRole, Qt::MatchFixedString);
        if (Index >= 0)
        {
            E->Combo->setCurrentIndex(Index);
        }
        Changed = true;
    }
    QtSettingsPage::ResetPage();
    if (Changed)
    {
        Q_EMIT PageChanged();
    }
}

// ------------------------------------------------------------- Directories

// the widgets belong to their Qt parents; the rows only point at them
QtDirectoriesPage::~QtDirectoriesPage()
{
    qDeleteAll(m_Rows);
}

QtDirectoriesPage::QtDirectoriesPage(QWidget * parent) :
    QtSettingsPage(parent)
{
    QVBoxLayout * Layout = new QVBoxLayout(this);
    AddRow(Layout, DIR_PLUGIN, "$AppPath/Plugin/", DIR_SELECT_PLUGIN, Directory_PluginSelected, Directory_PluginUseSelected);
    AddRow(Layout, DIR_AUTO_SAVE, "$AppPath/Save/", DIR_SELECT_AUTO, Directory_NativeSaveSelected, Directory_NativeSaveUseSelected);
    AddRow(Layout, DIR_INSTANT_SAVE, "$AppPath/Save/", DIR_SELECT_INSTANT, Directory_InstantSaveSelected, Directory_InstantSaveUseSelected);
    AddRow(Layout, DIR_SCREEN_SHOT, "$AppPath/Screenshots/", DIR_SELECT_SCREEN, Directory_SnapShotSelected, Directory_SnapShotUseSelected);
    AddRow(Layout, DIR_TEXTURE, "$AppPath/Textures/", DIR_SELECT_TEXTURE, Directory_TextureSelected, Directory_TextureUseSelected);
    Layout->addStretch(1);
    UpdatePageSettings();
}

LanguageStringID QtDirectoriesPage::PageTitle()
{
    return TAB_DIRECTORY;
}

void QtDirectoriesPage::AddRow(QVBoxLayout * Layout, LanguageStringID Title, const char * DefaultText, LanguageStringID SelectTitle, SettingID SelectedID, SettingID UseSelectedID)
{
    QGroupBox * Group = new QGroupBox(QtGS(Title), this);
    QVBoxLayout * Box = new QVBoxLayout(Group);
    DirRow * Row = new DirRow();
    Row->Default = new QRadioButton(QString::fromUtf8(DefaultText), Group);
    Row->Selected = new QRadioButton(Group);
    Row->Edit = new QLineEdit(Group);
    QPushButton * BrowseButton = new QPushButton("...", Group);
    BrowseButton->setProperty("vx", "browse"); // (the theme: no padding, a square of the height of the controls)
    Row->SelectedID = SelectedID;
    Row->UseSelectedID = UseSelectedID;
    Row->SelectTitle = SelectTitle;
    Row->EditState.Widget = Row->Edit;
    Row->DefaultState.Widget = Row->Default;

    QHBoxLayout * Line = new QHBoxLayout;
    Line->addWidget(Row->Selected);
    Line->addWidget(Row->Edit, 1);
    Line->addWidget(BrowseButton);
    Box->addWidget(Row->Default);
    Box->addLayout(Line);
    Layout->addWidget(Group);
    m_Rows.push_back(Row);

    // UseSelectedClicked / *DirChanged of the WTL page.
    connect(Row->Default, &QRadioButton::clicked, this, [this, Row]() { MarkChanged(Row->DefaultState); });
    connect(Row->Selected, &QRadioButton::clicked, this, [this, Row]() { MarkChanged(Row->DefaultState); });
    connect(Row->Edit, &QLineEdit::textEdited, this, [this, Row]() { MarkChanged(Row->EditState); });
    connect(BrowseButton, &QPushButton::clicked, this, [this, Row]() { Browse(*Row); });
}

void QtDirectoriesPage::Browse(DirRow & Row)
{
    QString Dir = QFileDialog::getExistingDirectory(this, QtGS(Row.SelectTitle), Row.Edit->text());
    if (Dir.isEmpty())
    {
        return;
    }
    if (!Dir.endsWith('/'))
    {
        Dir += '/';
    }
    Row.Edit->setText(Dir);
    Row.Selected->setChecked(true);
    SetChanged(Row.EditState, true);
    MarkChanged(Row.DefaultState);
}

void QtDirectoriesPage::UpdatePageSettings()
{
    for (DirRow * Row : m_Rows)
    {
        std::string Directory;
        SetChanged(Row->EditState, g_Settings->LoadStringVal(Row->SelectedID, Directory));
        Row->Edit->setText(QString::fromStdString(Directory));

        bool UseSelected = false;
        SetChanged(Row->DefaultState, g_Settings->LoadBool(Row->UseSelectedID, UseSelected));
        Row->Default->setChecked(!UseSelected);
        Row->Selected->setChecked(UseSelected);
    }
}

void QtDirectoriesPage::ApplySettings(bool UpdateScreen)
{
    for (DirRow * Row : m_Rows)
    {
        if (Row->EditState.Changed)
        {
            g_Settings->SaveString(Row->SelectedID, Row->Edit->text().toStdString());
        }
        if (Row->EditState.Reset)
        {
            g_Settings->DeleteSetting(Row->SelectedID);
            Row->EditState.Reset = false;
        }
    }
    for (DirRow * Row : m_Rows)
    {
        if (Row->DefaultState.Changed)
        {
            bool UseSelected = !Row->Default->isChecked();
            g_Settings->SaveBool(Row->UseSelectedID, UseSelected);
            if (Row->UseSelectedID == Directory_TextureUseSelected && !UseSelected)
            {
                g_Settings->DeleteSetting(Directory_TextureSelected);
            }
        }
        if (Row->DefaultState.Reset)
        {
            g_Settings->DeleteSetting(Row->UseSelectedID);
            Row->DefaultState.Reset = false;
        }
    }
    if (UpdateScreen)
    {
        UpdatePageSettings();
    }
}

bool QtDirectoriesPage::EnableReset()
{
    for (const DirRow * Row : m_Rows)
    {
        if (Row->EditState.Changed || Row->DefaultState.Changed)
        {
            return true;
        }
    }
    return false;
}

void QtDirectoriesPage::ResetPage()
{
    for (DirRow * Row : m_Rows)
    {
        if (Row->EditState.Changed)
        {
            Row->Edit->setText(QString::fromStdString(g_Settings->LoadDefaultString(Row->SelectedID)));
            SetReset(Row->EditState, true);
        }
        if (Row->DefaultState.Changed)
        {
            bool UseSelected = g_Settings->LoadDefaultBool(Row->UseSelectedID);
            Row->Default->setChecked(!UseSelected);
            Row->Selected->setChecked(UseSelected);
            SetReset(Row->DefaultState, true);
        }
    }
    Q_EMIT PageChanged();
}

// -------------------------------------------------------------- Disk drive

QtDiskDrivePage::QtDiskDrivePage(QWidget * parent) :
    QtSettingsPage(parent)
{
    QVBoxLayout * Layout = new QVBoxLayout(this);
    QFormLayout * Form = new QFormLayout;
    Layout->addLayout(Form);

    AddIplRow(Form, OPTION_IPL_ROM_PATH, File_DiskIPLPath);
    AddIplRow(Form, OPTION_IPL_ROM_USA_PATH, File_DiskIPLUSAPath);
    AddIplRow(Form, OPTION_IPL_ROM_TOOL_PATH, File_DiskIPLTOOLPath);

    QLabel * SaveText = new QLabel(QtGS(OPTION_DISKSAVETYPE), this);
    QComboBox * SaveType = new QComboBox(this);
    Form->addRow(SaveText, SaveType);
    Layout->addStretch(1);

    Modified * Item = AddModComboBox(SaveType, Setting_DiskSaveType);
    if (Item != nullptr)
    {
        Item->TextField = SaveText;
        AddItem(SaveType, DISKSAVE_SHADOW, SaveDisk_ShadowFile);
        AddItem(SaveType, DISKSAVE_RAM, SaveDisk_RAMFile);
    }
    UpdatePageSettings();
}

LanguageStringID QtDiskDrivePage::PageTitle()
{
    return TAB_DISKDRIVE;
}

void QtDiskDrivePage::AddIplRow(QFormLayout * Form, LanguageStringID Text, SettingID Type)
{
    QLabel * Label = new QLabel(QtGS(Text), this);
    QLineEdit * Edit = new QLineEdit(this);
    QPushButton * Browse = new QPushButton("...", this);
    Browse->setProperty("vx", "browse");
    QHBoxLayout * Row = new QHBoxLayout;
    Row->addWidget(Edit, 1);
    Row->addWidget(Browse);
    Form->addRow(Label, Row);
    AddModTextBox(Edit, Type, true)->TextField = Label;

    // SelectFile(): same file-name filter as upstream, starting in the ROM
    // directory.
    connect(Browse, &QPushButton::clicked, this, [this, Edit, Type]() {
        QString File = QFileDialog::getOpenFileName(
            this, QtGS(DIR_SELECT_PLUGIN), QString::fromStdString(g_Settings->LoadStringVal(RomList_GameDir)),
            "64DD IPL ROM image (*.zip *.7z *.?64 *.rom *.usa *.jap *.pal *.bin);;All files (*)");
        if (File.isEmpty())
        {
            return;
        }
        Edit->setText(File);
        Q_EMIT Edit->textEdited(File);
    });
}

// ---------------------------------------------------------------- Game pages

QtGameGeneralPage::QtGameGeneralPage(QWidget * parent) :
    QtSettingsPage(parent)
{
    QVBoxLayout * Layout = new QVBoxLayout(this);
    QFormLayout * Form = new QFormLayout;
    Layout->addLayout(Form);

    Form->addRow(new QLabel(QtGS(RB_GOODNAME), this), new QLabel(QString::fromStdString(g_Settings->LoadStringVal(Rdb_GoodName)), this));

    QLabel * MemText = new QLabel(QtGS(ROM_MEM_SIZE), this);
    QComboBox * MemSize = new QComboBox(this);
    Form->addRow(MemText, MemSize);
    QLabel * SaveText = new QLabel(QtGS(ROM_SAVE_TYPE), this);
    QComboBox * SaveType = new QComboBox(this);
    Form->addRow(SaveText, SaveType);
    QLabel * CounterText = new QLabel(QtGS(ROM_COUNTER_FACTOR), this);
    QComboBox * CounterFactor = new QComboBox(this);
    Form->addRow(CounterText, CounterFactor);
    QLabel * ViText = new QLabel(QtGS(ROM_VIREFRESH), this);
    QLineEdit * ViRefresh = new QLineEdit(this);
    Form->addRow(ViText, ViRefresh);
    QLabel * AiText = new QLabel(QtGS(ROM_COUNTPERBYTE), this);
    QLineEdit * AiCount = new QLineEdit(this);
    Form->addRow(AiText, AiCount);
    QLabel * OverclockText = new QLabel(QtGS(ROM_OVER_CLOCK_MODIFIER), this);
    QLineEdit * Overclock = new QLineEdit(this);
    Form->addRow(OverclockText, Overclock);
    QLabel * DelaySiText = new QLabel(QtGS(ROM_DELAY_SI), this);
    QLineEdit * DelaySi = new QLineEdit(this);
    Form->addRow(DelaySiText, DelaySi);

    QCheckBox * FixedAudio = new QCheckBox(QtGS(ROM_FIXED_AUDIO), this);
    QCheckBox * SyncAudio = new QCheckBox(QtGS(ROM_SYNC_AUDIO), this);
    QCheckBox * DelayDp = new QCheckBox(QtGS(ROM_DELAY_DP), this);
    QCheckBox * AudioSignal = new QCheckBox(QtGS(ROM_AUDIO_SIGNAL), this);
    QCheckBox * UnalignedDma = new QCheckBox(QtGS(ROM_UNALIGNED_DMA), this);
    QCheckBox * Randomize = new QCheckBox(QtGS(ROM_RANDOMIZE_SIPI_INTERRUPTS), this);
    for (QCheckBox * C : {FixedAudio, SyncAudio, DelayDp, AudioSignal, UnalignedDma, Randomize})
    {
        Layout->addWidget(C);
    }
    Layout->addStretch(1);

    AddModCheckBox(SyncAudio, Game_SyncViaAudio);
    AddModCheckBox(FixedAudio, Game_FixedAudio);
    AddModCheckBox(DelayDp, Game_DelayDP);
    AddModCheckBox(AudioSignal, Game_RspAudioSignal);
    AddModCheckBox(UnalignedDma, Game_UnalignedDMA);
    AddModCheckBox(Randomize, Game_RandomizeSIPIInterrupts);

    Modified * Item = AddModComboBox(MemSize, Game_RDRamSize);
    if (Item != nullptr)
    {
        Item->TextField = MemText;
        AddItem(MemSize, RDRAM_4MB, 0x400000);
        AddItem(MemSize, RDRAM_8MB, 0x800000);
    }
    Item = AddModComboBox(SaveType, Game_SaveChip);
    if (Item != nullptr)
    {
        Item->TextField = SaveText;
        AddItem(SaveType, SAVE_FIRST_USED, (uint32_t)(int64_t)SaveChip_Auto);
        AddItem(SaveType, SAVE_4K_EEPROM, SaveChip_Eeprom_4K);
        AddItem(SaveType, SAVE_16K_EEPROM, SaveChip_Eeprom_16K);
        AddItem(SaveType, SAVE_SRAM, SaveChip_Sram);
        AddItem(SaveType, SAVE_FLASHRAM, SaveChip_FlashRam);
    }
    Item = AddModComboBox(CounterFactor, Game_CounterFactor);
    if (Item != nullptr)
    {
        Item->TextField = CounterText;
        AddItem(CounterFactor, NUMBER_1, 1);
        AddItem(CounterFactor, NUMBER_2, 2);
        AddItem(CounterFactor, NUMBER_3, 3);
        AddItem(CounterFactor, NUMBER_4, 4);
        AddItem(CounterFactor, NUMBER_5, 5);
        AddItem(CounterFactor, NUMBER_6, 6);
    }
    AddModTextBox(ViRefresh, Game_ViRefreshRate, false)->TextField = ViText;
    AddModTextBox(AiCount, Game_AiCountPerBytes, false)->TextField = AiText;
    AddModTextBox(Overclock, Game_OverClockModifier, false)->TextField = OverclockText;
    AddModTextBox(DelaySi, Game_DelaySI, false)->TextField = DelaySiText;

    if (!g_Settings->LoadBool(Setting_SyncViaAudioEnabled))
    {
        SyncAudio->setEnabled(false);
    }
    UpdatePageSettings();
}

LanguageStringID QtGameGeneralPage::PageTitle()
{
    return TAB_ROMSETTINGS;
}

QtGameRecompilerPage::QtGameRecompilerPage(QWidget * parent) :
    QtSettingsPage(parent)
{
    QVBoxLayout * Layout = new QVBoxLayout(this);
    QFormLayout * Form = new QFormLayout;
    Layout->addLayout(Form);

    QLabel * CpuText = new QLabel(QtGS(ROM_CPU_STYLE), this);
    QComboBox * CpuType = new QComboBox(this);
    Form->addRow(CpuText, CpuType);
    QLabel * FindText = new QLabel(QtGS(ROM_FUNC_FIND), this);
    QComboBox * FuncFind = new QComboBox(this);
    Form->addRow(FindText, FuncFind);
    QLabel * LinkText = new QLabel(QtGS(ADVANCE_ABL), this);
    QComboBox * BlockLinking = new QComboBox(this);
    Form->addRow(LinkText, BlockLinking);

    QCheckBox * RegCache = new QCheckBox(QtGS(ROM_REG_CACHE), this);
    QCheckBox * FpuRegCache = new QCheckBox(QtGS(ROM_FPU_REG_CACHE), this);
    QCheckBox * Bit32 = new QCheckBox(QtGS(ROM_32BIT), this);
    QCheckBox * FastSp = new QCheckBox(QtGS(ROM_FAST_SP), this);
    QGridLayout * Grid = new QGridLayout;
    Grid->addWidget(RegCache, 0, 0);
    Grid->addWidget(Bit32, 0, 1);
    Grid->addWidget(FpuRegCache, 1, 0);
    Grid->addWidget(FastSp, 1, 1);
    Layout->addLayout(Grid);

    QGroupBox * SelfMod = new QGroupBox(QtGS(ADVANCE_SMCM), this);
    QGridLayout * SmmGrid = new QGridLayout(SelfMod);
    QCheckBox * SmmCache = new QCheckBox(QtGS(ADVANCE_SMM_CACHE), SelfMod);
    QCheckBox * SmmStore = new QCheckBox(Vortex::Tr("Instrucción Store", "Store Instruction") /* text of IDC_SMM_STORE in UIResources.rc */, SelfMod);
    QCheckBox * SmmDma = new QCheckBox(QtGS(ADVANCE_SMM_PIDMA), SelfMod);
    QCheckBox * SmmTlb = new QCheckBox(QtGS(ADVANCE_SMM_TLB), SelfMod);
    QCheckBox * SmmValidate = new QCheckBox(QtGS(ADVANCE_SMM_VALIDATE), SelfMod);
    SmmGrid->addWidget(SmmCache, 0, 0);
    SmmGrid->addWidget(SmmStore, 0, 1);
    SmmGrid->addWidget(SmmDma, 1, 0);
    SmmGrid->addWidget(SmmTlb, 1, 1);
    SmmGrid->addWidget(SmmValidate, 2, 0);
    Layout->addWidget(SelfMod);
    Layout->addStretch(1);

    AddModCheckBox(RegCache, Game_RegCache);
    AddModCheckBox(FpuRegCache, Game_FPURegCache);
    AddModCheckBox(SmmCache, Game_SMM_Cache);
    AddModCheckBox(SmmDma, Game_SMM_PIDMA);
    AddModCheckBox(SmmValidate, Game_SMM_ValidFunc);
    AddModCheckBox(SmmTlb, Game_SMM_TLB);
    AddModCheckBox(SmmStore, Game_SMM_StoreInstruc);
    AddModCheckBox(FastSp, Game_FastSP);
    AddModCheckBox(Bit32, Game_32Bit);

    if (AddModComboBox(BlockLinking, Game_BlockLinkingMode) != nullptr)
    {
        AddItem(BlockLinking, ROM_BLOCK_LINK_NONE, BlockLinking_None);
        AddItem(BlockLinking, ROM_BLOCK_LINK_EAGER, BlockLinking_Eager);
        AddItem(BlockLinking, ROM_BLOCK_LINK_ADAPTIVE, BlockLinking_Adaptive);
    }
    if (AddModComboBox(CpuType, Game_CpuType) != nullptr)
    {
        AddItem(CpuType, CORE_RECOMPILER, CPU_Recompiler);
        AddItem(CpuType, CORE_INTERPTER, CPU_Interpreter);
        if (g_DebugSettings.haveDebugger)
        {
            AddItem(CpuType, CORE_SYNC, CPU_SyncCores);
        }
    }
    if (AddModComboBox(FuncFind, Game_FuncLookupMode) != nullptr)
    {
        AddItem(FuncFind, FLM_PLOOKUP, FuncFind_PhysicalLookup);
        AddItem(FuncFind, FLM_VLOOKUP, FuncFind_VirtualLookup);
    }
    UpdatePageSettings();
}

LanguageStringID QtGameRecompilerPage::PageTitle()
{
    return TAB_RECOMPILER;
}

QtGameDiskDrivePage::QtGameDiskDrivePage(QWidget * parent) :
    QtSettingsPage(parent)
{
    QVBoxLayout * Layout = new QVBoxLayout(this);
    QFormLayout * Form = new QFormLayout;
    Layout->addLayout(Form);
    QLabel * SeekText = new QLabel(QtGS(ROM_DISK_SEEK_TIMING), this);
    QComboBox * SeekTiming = new QComboBox(this);
    Form->addRow(SeekText, SeekTiming);
    Layout->addStretch(1);
    if (AddModComboBox(SeekTiming, Game_DiskSeekTiming) != nullptr)
    {
        AddItem(SeekTiming, ROM_DISK_SEEK_TIMING_TURBO, DiskSeek_Turbo);
        AddItem(SeekTiming, ROM_DISK_SEEK_TIMING_SLOW, DiskSeek_Slow);
    }
    UpdatePageSettings();
}

LanguageStringID QtGameDiskDrivePage::PageTitle()
{
    return TAB_DISKSETTINGS;
}

// ---------------------------------------------------------------- Game browser

QtGameBrowserPage::QtGameBrowserPage(QWidget * parent) :
    QtSettingsPage(parent),
    m_OrderChanged(false),
    m_OrderReset(false)
{
    QVBoxLayout * Layout = new QVBoxLayout(this);
    m_UseBrowser = new QCheckBox(QtGS(RB_USE), this);
    Layout->addWidget(m_UseBrowser);

    QFrame * Line = new QFrame(this);
    Line->setFrameShape(QFrame::HLine);
    Line->setFrameShadow(QFrame::Sunken);
    Layout->addWidget(Line);

    QGridLayout * Grid = new QGridLayout;
    QLabel * AvailableText = new QLabel(QtGS(RB_AVALIABLE_FIELDS), this);
    QLabel * UsingText = new QLabel(QtGS(RB_SHOW_FIELDS), this);
    m_Available = new QListWidget(this);
    m_Using = new QListWidget(this);
    QPushButton * Add = new QPushButton(QtGS(RB_ADD), this);
    QPushButton * Remove = new QPushButton(QtGS(RB_REMOVE), this);
    QPushButton * Up = new QPushButton(QtGS(RB_UP), this);
    QPushButton * Down = new QPushButton(QtGS(RB_DOWN), this);
    QVBoxLayout * Middle = new QVBoxLayout;
    Middle->addWidget(Add);
    Middle->addWidget(Remove);
    Middle->addStretch(1);
    QHBoxLayout * UpDown = new QHBoxLayout;
    UpDown->addWidget(Up);
    UpDown->addWidget(Down);
    Grid->addWidget(AvailableText, 0, 0);
    Grid->addWidget(UsingText, 0, 2);
    Grid->addWidget(m_Available, 1, 0);
    Grid->addLayout(Middle, 1, 1);
    Grid->addWidget(m_Using, 1, 2);
    Grid->addLayout(UpDown, 2, 2);
    Layout->addLayout(Grid, 1);

    QCheckBox * Recursion = new QCheckBox(QtGS(RB_DIR_RECURSION), this);
    QCheckBox * Extensions = new QCheckBox(QtGS(RB_FILEEXT), this);
    Layout->addWidget(Recursion);
    Layout->addWidget(Extensions);

    m_FieldControls = {AvailableText, UsingText, m_Available, m_Using, Add, Remove, Up, Down, Recursion};

    AddModCheckBox(m_UseBrowser, (SettingID)RomBrowser_Enabled);
    AddModCheckBox(Recursion, RomList_GameDirRecursive);
    AddModCheckBox(Extensions, RomList_ShowFileExtensions);
    connect(m_UseBrowser, &QCheckBox::clicked, this, [this]() { FixCtrlState(); });

    QtRomBrowser::GetFieldInfo(m_Fields);

    // AddFieldClicked / RemoveFieldClicked / MoveFieldUp/DownClicked
    connect(Add, &QPushButton::clicked, this, [this]() {
        const int Row = m_Available->currentRow();
        if (Row < 0)
        {
            return;
        }
        const int Field = m_Available->item(Row)->data(Qt::UserRole).toInt();
        delete m_Available->takeItem(Row);
        m_Available->setCurrentRow(std::min(Row, m_Available->count() - 1));
        AddItemTo(m_Using, Field);
        m_Using->setCurrentRow(m_Using->count() - 1);
        m_OrderChanged = true;
        m_OrderReset = false;
        Q_EMIT PageChanged();
    });
    connect(Remove, &QPushButton::clicked, this, [this]() {
        const int Row = m_Using->currentRow();
        if (Row < 0)
        {
            return;
        }
        const int Field = m_Using->item(Row)->data(Qt::UserRole).toInt();
        delete m_Using->takeItem(Row);
        m_Using->setCurrentRow(std::min(Row, m_Using->count() - 1));
        AddItemTo(m_Available, Field);
        m_Available->setCurrentRow(m_Available->count() - 1);
        m_OrderChanged = true;
        m_OrderReset = false;
        Q_EMIT PageChanged();
    });
    connect(Up, &QPushButton::clicked, this, [this]() {
        const int Row = m_Using->currentRow();
        if (Row <= 0)
        {
            return;
        }
        QListWidgetItem * Item = m_Using->takeItem(Row);
        m_Using->insertItem(Row - 1, Item);
        m_Using->setCurrentRow(Row - 1);
        m_OrderChanged = true;
        m_OrderReset = false;
        Q_EMIT PageChanged();
    });
    connect(Down, &QPushButton::clicked, this, [this]() {
        const int Row = m_Using->currentRow();
        if (Row < 0 || Row >= m_Using->count() - 1)
        {
            return;
        }
        QListWidgetItem * Item = m_Using->takeItem(Row);
        m_Using->insertItem(Row + 1, Item);
        m_Using->setCurrentRow(Row + 1);
        m_OrderChanged = true;
        m_OrderReset = false;
        Q_EMIT PageChanged();
    });

    UpdatePageSettings();
}

LanguageStringID QtGameBrowserPage::PageTitle()
{
    return TAB_ROMSELECTION;
}

void QtGameBrowserPage::AddItemTo(QListWidget * List, int FieldIndex, int Position)
{
    QListWidgetItem * Item = new QListWidgetItem(QtGS(m_Fields[FieldIndex].LangID()));
    Item->setData(Qt::UserRole, FieldIndex);
    if (Position < 0 || Position > List->count())
    {
        List->addItem(Item);
    }
    else
    {
        List->insertItem(Position, Item);
    }
}

// COptionsGameBrowserPage::UpdateFieldList
void QtGameBrowserPage::UpdateFieldList(const ROMBROWSER_FIELDS_LIST & Fields)
{
    m_Available->clear();
    m_Using->clear();
    m_OrderChanged = false;
    for (size_t i = 0, n = Fields.size(); i < n; i++)
    {
        if (Fields[i].PosChanged())
        {
            m_OrderChanged = true;
        }
        int Pos = (int)Fields[i].Pos();
        if (Pos < 0)
        {
            AddItemTo(m_Available, (int)i);
            continue;
        }
        AddItemTo(m_Using, (int)i, std::min(Pos, m_Using->count()));
    }
    m_Available->sortItems();
}

void QtGameBrowserPage::FixCtrlState()
{
    const bool Enabled = m_UseBrowser->isChecked();
    for (QWidget * W : m_FieldControls)
    {
        W->setEnabled(Enabled);
    }
}

void QtGameBrowserPage::UpdatePageSettings()
{
    UpdateFieldList(m_Fields);
    QtSettingsPage::UpdatePageSettings();
    FixCtrlState();
}

void QtGameBrowserPage::ApplySettings(bool UpdateScreen)
{
    bool ColumnsChanged = false;
    if (m_OrderReset)
    {
        for (ROMBROWSER_FIELDS & Field : m_Fields)
        {
            Field.ResetPos();
        }
        ColumnsChanged = true;
    }
    else
    {
        for (int Item = 0; Item < m_Using->count(); Item++)
        {
            const int Field = m_Using->item(Item)->data(Qt::UserRole).toInt();
            if (m_Fields[Field].Pos() != (size_t)Item)
            {
                m_Fields[Field].SetColPos(Item);
                ColumnsChanged = true;
            }
        }
        for (int Item = 0; Item < m_Available->count(); Item++)
        {
            const int Field = m_Available->item(Item)->data(Qt::UserRole).toInt();
            if (m_Fields[Field].Pos() != (size_t)-1)
            {
                m_Fields[Field].SetColPos(-1);
                ColumnsChanged = true;
            }
        }
    }
    if (ColumnsChanged)
    {
        // The ROM browser reloads its columns when this flag flips.
        UISettingsSaveBool(RomBrowser_ColoumnsChanged, !UISettingsLoadBool(RomBrowser_ColoumnsChanged));
    }
    QtSettingsPage::ApplySettings(UpdateScreen);
}

bool QtGameBrowserPage::EnableReset()
{
    return m_OrderChanged || QtSettingsPage::EnableReset();
}

void QtGameBrowserPage::ResetPage()
{
    if (m_OrderChanged)
    {
        ROMBROWSER_FIELDS_LIST Fields;
        QtRomBrowser::GetFieldInfo(Fields, true);
        UpdateFieldList(Fields);
        m_OrderReset = true;
        Q_EMIT PageChanged();
    }
    QtSettingsPage::ResetPage();
}

// CGameStatusPage: "Status" is chosen among the keys of the RDB's [ROM Status]
// section (entries ending in .Sel/.Auto are internal), the notes are free text.
QtGameStatusPage::QtGameStatusPage(QWidget * parent) :
    QtSettingsPage(parent)
{
    QVBoxLayout * Layout = new QVBoxLayout(this);
    QFormLayout * Form = new QFormLayout;
    Layout->addLayout(Form);

    QLabel * StatusText = new QLabel(Vortex::Tr("Estado:", "Status:"), this);
    QComboBox * StatusType = new QComboBox(this);
    Form->addRow(StatusText, StatusType);
    QLabel * CoreText = new QLabel(Vortex::Tr("Notas (núcleo)", "Notes (Core)"), this);
    QLineEdit * CoreNotes = new QLineEdit(this);
    Form->addRow(CoreText, CoreNotes);
    QLabel * PluginText = new QLabel(Vortex::Tr("Notas (plugin)", "Notes (Plugin)"), this);
    QLineEdit * PluginNotes = new QLineEdit(this);
    Form->addRow(PluginText, PluginNotes);
    Layout->addStretch(1);

    CIniFile RomIniFile(CSettingTypeRomDatabase::DatabasePath(g_Settings->LoadStringVal(SupportFile_RomDatabase)).c_str());
    CIniFile::strlist Keys;
    RomIniFile.GetKeyList("ROM Status", Keys);
    if (Modified * Item = AddModComboBoxTxt(StatusType, (SettingID)Rdb_Status))
    {
        for (const std::string & Key : Keys)
        {
            if (strstr(Key.c_str(), ".Sel") != nullptr || strstr(Key.c_str(), ".Auto") != nullptr)
            {
                continue;
            }
            StatusType->addItem(QString::fromStdString(Key), QString::fromStdString(Key));
        }
        Item->TextField = StatusText;
    }
    AddModTextBox(CoreNotes, (SettingID)Rdb_NotesCore, true)->TextField = CoreText;
    AddModTextBox(PluginNotes, (SettingID)Rdb_NotesPlugin, true)->TextField = PluginText;
    UpdatePageSettings();
}

LanguageStringID QtGameStatusPage::PageTitle()
{
    return TAB_ROMNOTES;
}
