// Regression test for the Qt Settings pages (Source/Vortex64-gui/Settings).
// Runs with QT_QPA_PLATFORM=offscreen and an isolated XDG config/data home.
//
//   settings-test write   toggles bound controls, applies, resets, applies
//   settings-test read    checks that the values written by "write" persisted
//   settings-test sweep-write / sweep-read   every control of every page: UI -> SettingID -> persistence -> UI
//   settings-test lookup  how a game is found in the ROM database (USA, Europe, Japan, a game that is not there; the three orders of the header) and the memory it gets
//   settings-test rdb     the ROM database of a read-only installation (user copy under XDG)
//
// A control bound to a SettingID must (1) become "changed" when the user edits
// it, (2) write the SettingID on Apply, (3) return to the default on Reset +
// Apply, and (4) survive a restart.
#include "../Settings/SettingsDialog.h"
#include "../Settings/SettingsPages.h"
#include "../Settings/ShortCutsPage.h"
#include "../Settings/VortexPages.h"
#include "../PresentSync.h"

#include <Common/path.h>
#include <Vortex64-cli/LinuxNotification.h>
#include <Vortex64-core/AppInit.h>
#include <Vortex64-core/N64System/N64Rom.h>
#include <Vortex64-core/N64System/N64Types.h>
#include <Vortex64-core/Settings.h>
#include <Vortex64-core/Settings/SettingType/SettingsType-RomDatabase.h>
#include <Vortex64-core/N64System/SystemGlobals.h>

#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QPixmap>
#include <QGroupBox>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QTreeWidget>
#include <QLabel>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

static int g_Failures = 0;
#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            g_Failures++;                                                      \
        }                                                                      \
    } while (0)

static QCheckBox * CheckWithText(QWidget * Page, LanguageStringID ID)
{
    for (QCheckBox * C : Page->findChildren<QCheckBox *>())
    {
        if (C->text() == QtGS(ID))
        {
            return C;
        }
    }
    return nullptr;
}

static int DoWrite()
{
    QtGeneralOptionsPage General;
    QtDefaultsOptionsPage Defaults;

    // Checkbox: Setting_AutoFullscreen defaults to false.
    QCheckBox * Full = CheckWithText(&General, OPTION_AUTO_FULLSCREEN);
    CHECK(Full != nullptr);
    CHECK(!g_Settings->LoadBool((SettingID)Setting_AutoFullscreen));
    CHECK(!Full->isChecked());
    CHECK(!General.EnableReset());
    Full->click();
    CHECK(Full->font().bold());
    CHECK(General.EnableReset());
    General.ApplySettings(true);
    CHECK(g_Settings->LoadBool((SettingID)Setting_AutoFullscreen));

    // Numeric edit box: Directory_RecentGameDirCount defaults to 10.
    // Second edit box of the page (creation order): remembered directories.
    QLineEdit * Dirs = General.findChildren<QLineEdit *>().at(1);
    CHECK(Dirs != nullptr);
    CHECK(Dirs->text() == "10");
    Dirs->setText("7");
    Q_EMIT Dirs->textEdited("7");
    General.ApplySettings(true);
    CHECK(g_Settings->LoadDword((SettingID)Directory_RecentGameDirCount) == 7);

    // Combo box: Default_CounterFactor.
    QComboBox * Counter = Defaults.findChildren<QComboBox *>().at(2);
    int Index = Counter->findData(QVariant::fromValue<uint>(3));
    CHECK(Index >= 0);
    Counter->setCurrentIndex(Index);
    Q_EMIT Counter->activated(Index);
    Defaults.ApplySettings(true);
    CHECK(g_Settings->LoadDword(Default_CounterFactor) == 3);

    // Reset page + Apply returns the SettingID to its default.
    Defaults.ResetPage();
    Defaults.ApplySettings(true);
    CHECK(g_Settings->LoadDword(Default_CounterFactor) == g_Settings->LoadDefaultDword(Default_CounterFactor));
    CHECK(!Defaults.EnableReset());

    // Plugin page: discovery by scanning the plugin directory for .so files.
    g_Settings->SaveString(Directory_Plugin, CPath(CPath::MODULE_DIRECTORY).GetDirectory());
    QtPluginPage Plugins(false);
    QList<QComboBox *> Combos = Plugins.findChildren<QComboBox *>();
    CHECK(Combos.size() == 4);
    for (QComboBox * C : Combos)
    {
        CHECK(C->count() >= 1);
    }
    CHECK(Combos.at(0)->currentData().toString() == "libVortex64-video.so");
    CHECK(Combos.at(1)->currentData().toString() == "libVortex64-audio.so");
    CHECK(Combos.at(2)->currentData().toString() == "libVortex64-input.so");
    CHECK(Combos.at(3)->currentData().toString() == "libVortex64-rsp.so");
    // helper modules (Qt configuration dialogs) are not plugins and must not be listed
    for (QComboBox * C : Combos)
    {
        for (int i = 0; i < C->count(); i++)
        {
            CHECK(!C->itemData(i).toString().contains("qtconfig"));
        }
    }
    CHECK(!Plugins.EnableReset());
    for (QPushButton * B : Plugins.findChildren<QPushButton *>())
    {
        (void)B; // About buttons are enabled only for plugins exporting DllAbout
    }

    // Directories page: choosing a custom directory stores the path and the
    // "use selected" flag (upstream: Directory_*Selected / *UseSelected).
    QtDirectoriesPage DirPage;
    QList<QLineEdit *> DirEdits = DirPage.findChildren<QLineEdit *>();
    QList<QRadioButton *> Radios = DirPage.findChildren<QRadioButton *>();
    CHECK(DirEdits.size() == 5 && Radios.size() == 10);
    CHECK(!g_Settings->LoadBool(Directory_SnapShotUseSelected));
    DirEdits.at(3)->setText("/tmp/pj64-shots/");
    Q_EMIT DirEdits.at(3)->textEdited("/tmp/pj64-shots/");
    Radios.at(7)->click(); // "selected" radio of the Screenshots row
    DirPage.ApplySettings(true);
    CHECK(g_Settings->LoadBool(Directory_SnapShotUseSelected));
    CHECK(g_Settings->LoadStringVal(Directory_SnapShotSelected) == "/tmp/pj64-shots/");
    CHECK(DirPage.EnableReset());
    DirPage.ResetPage();
    DirPage.ApplySettings(true);
    CHECK(!g_Settings->LoadBool(Directory_SnapShotUseSelected));
    CHECK(!DirPage.EnableReset());

    // Game recompiler page: CPU core style (Recompiler / Interpreter).
    QtGameRecompilerPage Recomp;
    QComboBox * Cpu = Recomp.findChildren<QComboBox *>().at(0);
    CHECK(Cpu->count() == 2);
    int Interp = Cpu->findData(QVariant::fromValue<uint>(CPU_Interpreter));
    CHECK(Interp >= 0);
    Cpu->setCurrentIndex(Interp);
    Q_EMIT Cpu->activated(Interp);
    Recomp.ApplySettings(true);
    CHECK(g_Settings->LoadDword(Game_CpuType) == CPU_Interpreter);
    Recomp.ResetPage();
    Recomp.ApplySettings(true);
    CHECK(!Recomp.EnableReset());

    // Game plugin page: "Default" entry first; choosing it leaves the per-game
    // setting unset.
    QtPluginPage GamePlugins(true);
    QComboBox * GameGfx = GamePlugins.findChildren<QComboBox *>().at(0);
    CHECK(GameGfx->itemData(0).toString().isEmpty());
    CHECK(GameGfx->currentIndex() == 0);
    CHECK(!GamePlugins.EnableReset());

    // The dialog lists its categories: General, Directories, Graphics, Audio, Input, RSP, Plugins, Shortcuts, 64DD and
    // (unless the advanced settings are hidden) Advanced; no game loaded -> no game section.
    {
        SettingsDialog Dialog(false);
        QListWidget * Nav = Dialog.findChild<QListWidget *>("VxSettingsNav");
        CHECK(Nav != nullptr);
        const QStringList Basic10 = {"general", "directories", "graphics", "audio", "input", "rsp", "plugins", "shortcuts", "disk"};
        // "Hide advanced settings" does not take the Advanced category away, over and over
        QCheckBox * BasicBox = nullptr;
        for (QWidget * Page : Dialog.findChildren<QtGeneralOptionsPage *>())
        {
            BasicBox = CheckWithText(Page, OPTION_BASIC_MODE);
        }
        CHECK(BasicBox != nullptr);
        if (BasicBox != nullptr)
        {
            auto Expected = [&](bool) {
                QStringList List = Basic10;
                List << "advanced"; // (always there: see SettingsDialog::UpdateAdvanced)
                return List;
            };
            const bool Start = BasicBox->isChecked();
            CHECK(Dialog.CategoryIds() == Expected(Start));
            for (int i = 0; i < 6; i++)
            {
                BasicBox->click();
                const bool Hide = BasicBox->isChecked();
                CHECK(Hide == ((i % 2 == 0) ? !Start : Start));
                CHECK(Dialog.CategoryIds() == Expected(Hide));
            }
            CHECK(BasicBox->isChecked() == Start);
            CHECK(Dialog.CategoryIds() == Expected(Start));
        }
        // every category opens
        for (const QString & Id : Dialog.CategoryIds())
        {
            Dialog.ShowPage(Id);
            CHECK(Dialog.CurrentCategory() == Id);
        }
    }

    // Vertical synchronization (Graphics): Auto, On, Off; Auto by default; the monitor that is detected is information
    {
        SettingsDialog Dialog(false);
        Vortex::VSyncPage * Sync = Dialog.findChild<Vortex::VSyncPage *>();
        CHECK(Sync != nullptr);
        if (Sync != nullptr)
        {
            QComboBox * Mode = Sync->Combo();
            CHECK(Mode != nullptr && Mode->count() == 3);
            CHECK(Mode->itemData(0).toUInt() == (uint)PJ64_VSYNC_AUTO && Mode->itemData(1).toUInt() == (uint)PJ64_VSYNC_ON && Mode->itemData(2).toUInt() == (uint)PJ64_VSYNC_OFF);
            CHECK(Mode->currentData().toUInt() == (uint)PJ64_VSYNC_AUTO);
            CHECK(Mode->itemText(0).contains("ecomend") || Mode->itemText(0).contains("ecommended"));
            const QString Text = Sync->DisplayLabel()->text();
            CHECK(Text.startsWith("Pantalla actual: ") || Text.startsWith("Current display: "));
            CHECK(!Text.contains("Hz") || Text.contains("@"));
            for (int Value : {PJ64_VSYNC_ON, PJ64_VSYNC_OFF, PJ64_VSYNC_AUTO})
            {
                const int Index = Mode->findData(QVariant::fromValue<uint>((uint)Value));
                Mode->setCurrentIndex(Index);
                Q_EMIT Mode->activated(Index);
                Sync->ApplySettings(true);
                CHECK(g_Settings->LoadDword(UserInterface_VSyncMode) == (uint32_t)Value);
            }
            // Maximum FPS: Automatic by default, the 11 presets, Custom...; persisted; the tooltips
            QComboBox * Fps = Sync->MaxFpsCombo();
            QSpinBox * Custom = Sync->MaxFpsCustom();
            CHECK(Fps != nullptr && Custom != nullptr && Fps->count() == 13);
            if (Fps != nullptr && Custom != nullptr && Fps->count() == 13)
            {
                const int Expected[] = {0, 20, 30, 50, 60, 75, 90, 100, 120, 144, 165, 240, -1};
                for (int i = 0; i < 13; i++)
                {
                    CHECK(Fps->itemData(i).toInt() == Expected[i]);
                }
                CHECK(Fps->itemText(0) == "Automático" || Fps->itemText(0) == "Automatic");
                CHECK(Fps->itemText(12) == QString::fromUtf8("Personalizado…") || Fps->itemText(12) == QString::fromUtf8("Custom…"));
                CHECK(Fps->itemText(5) == "75 FPS");
                CHECK(Fps->currentData().toInt() == 0 && g_Settings->LoadDword(UserInterface_MaxFps) == 0);
                CHECK(Fps->toolTip() == "Adapta la presentación a la pantalla y al contenido." || Fps->toolTip() == "Adapts presentation to the display and content.");
                CHECK(Custom->minimum() == 10 && Custom->maximum() == 1000);
                for (int Preset : {20, 30, 50, 60, 75, 90, 100, 120, 144, 165, 240})
                {
                    const int Index = Fps->findData(Preset);
                    Fps->setCurrentIndex(Index);
                    Q_EMIT Fps->activated(Index);
                    CHECK(Fps->toolTip() == "Limita la frecuencia de presentación sin cambiar la velocidad de emulación." || Fps->toolTip() == "Limits presentation rate without changing emulation speed.");
                    CHECK(Sync->ChosenMaxFps() == Preset);
                    CHECK(g_Settings->LoadDword(UserInterface_MaxFps) != (uint32_t)Preset || Preset == 0);
                    Sync->ApplySettings(true);
                    CHECK(g_Settings->LoadDword(UserInterface_MaxFps) == (uint32_t)Preset);
                    CHECK(Fps->currentData().toInt() == Preset);
                }
                // custom: a value of the box, in the range
                Fps->setCurrentIndex(Fps->findData(-1));
                Q_EMIT Fps->activated(Fps->currentIndex());
                Custom->setValue(37);
                CHECK(Sync->ChosenMaxFps() == 37);
                Sync->ApplySettings(true);
                CHECK(g_Settings->LoadDword(UserInterface_MaxFps) == 37 && Fps->currentData().toInt() == -1 && Custom->value() == 37);
                // what the box refuses
                Custom->setValue(0);
                CHECK(Custom->value() == 10);
                Custom->setValue(-5);
                CHECK(Custom->value() == 10);
                Custom->setValue(100000);
                CHECK(Custom->value() == 1000);
                Custom->setValue(1000);
                Sync->ApplySettings(true);
                CHECK(g_Settings->LoadDword(UserInterface_MaxFps) == 1000);
                // a new dialog shows what was saved; a value out of range in the file is Automatic
                {
                    SettingsDialog Again(false);
                    Vortex::VSyncPage * Page = Again.findChild<Vortex::VSyncPage *>();
                    CHECK(Page != nullptr && Page->MaxFpsCombo()->currentData().toInt() == -1 && Page->MaxFpsCustom()->value() == 1000 && Page->ChosenMaxFps() == 1000);
                }
                g_Settings->SaveDword(UserInterface_MaxFps, 5000000);
                {
                    SettingsDialog Again(false);
                    Vortex::VSyncPage * Page = Again.findChild<Vortex::VSyncPage *>();
                    CHECK(Page != nullptr && Page->MaxFpsCombo()->currentData().toInt() == 0 && Page->ChosenMaxFps() == 0);
                }
                // back to Automatic
                Fps->setCurrentIndex(0);
                Q_EMIT Fps->activated(0);
                Sync->ApplySettings(true);
                CHECK(g_Settings->LoadDword(UserInterface_MaxFps) == 0);
            }
        }
    }

    // ROM selection page: default fields (Name, Status displayed; 19 others
    // available); moving a field to the browser and applying stores its position.
    {
        QtGameBrowserPage Page;
        QList<QListWidget *> Lists = Page.findChildren<QListWidget *>();
        CHECK(Lists.size() == 2);
        CHECK(Lists.at(0)->count() == 19 && Lists.at(1)->count() == 2);
        CHECK(!Page.EnableReset());
        Lists.at(0)->setCurrentRow(0);
        // The available list is sorted (LBS_SORT); the top entry is added.
        const int AddedField = Lists.at(0)->item(0)->data(Qt::UserRole).toInt();
        QPushButton * Add = nullptr;
        for (QPushButton * B : Page.findChildren<QPushButton *>())
        {
            if (B->text() == QtGS(RB_ADD))
            {
                Add = B;
            }
        }
        CHECK(Add != nullptr);
        Add->click();
        CHECK(Lists.at(1)->count() == 3 && Page.EnableReset());
        Page.ApplySettings(true);
        // It was appended after Name and Status.
        uint32_t Pos = 99;
        CHECK(UISettingsLoadDwordIndex(RomBrowser_PosIndex, AddedField, Pos) && Pos == 2);
        Page.ResetPage();
        Page.ApplySettings(true);
        Pos = 99;
        CHECK(!UISettingsLoadDwordIndex(RomBrowser_PosIndex, AddedField, Pos));
        CHECK(Page.findChildren<QListWidget *>().at(1)->count() == 2);
    }

    // Game status page (RDB editor): the status combo lists the RDB's [ROM Status] keys
    {
        QtGameStatusPage Status;
        QList<QComboBox *> Combos = Status.findChildren<QComboBox *>();
        CHECK(Combos.size() == 1 && Combos.at(0)->count() >= 2);
        CHECK(Status.findChildren<QLineEdit *>().size() == 2);
    }

    // Keyboard shortcuts page: assign a key to a menu item, save it to Project64.sc3
    // and get it back from a fresh key table; reset drops the user shortcut.
    {
        auto UserKeys = [](int MenuID) {
            int Count = 0;
            CShortCuts Table;
            for (const CMenuShortCutKey & Key : Table.GetShortCuts().at(MenuID).GetAccelItems())
            {
                Count += (Key.UserAdded() && !Key.Inactive() && Key.Key() == 0x77 && Key.Ctrl()) ? 1 : 0;
            }
            return Count;
        };
        QtShortCutsPage Page;
        CHECK(!Page.EnableReset());
        // Pause only exists while a game runs: pick the "windowed" CPU state
        Page.findChildren<QComboBox *>().at(0)->setCurrentIndex(1);
        QTreeWidget * Items = Page.findChild<QTreeWidget *>();
        CHECK(Items != nullptr && Items->topLevelItemCount() >= 3);
        auto FindPause = [&]() {
            for (QTreeWidgetItemIterator It(Items); *It != nullptr; ++It)
            {
                if ((*It)->parent() != nullptr && (*It)->text(0) == "Pause")
                {
                    return *It;
                }
            }
            return static_cast<QTreeWidgetItem *>(nullptr);
        };
        QTreeWidgetItem * Pause = FindPause();
        CHECK(Pause != nullptr);
        if (Pause != nullptr)
        {
            Items->setCurrentItem(Pause);
            QComboBox * Keys = Page.findChildren<QComboBox *>().at(1);
            const int F8 = Keys->findData(0x77);
            CHECK(F8 >= 0);
            Keys->setCurrentIndex(F8);
            Q_EMIT Keys->activated(F8);
            for (QCheckBox * C : Page.findChildren<QCheckBox *>())
            {
                if (C->text() == "&Ctrl")
                {
                    C->click();
                }
            }
            for (QPushButton * B : Page.findChildren<QPushButton *>())
            {
                if (B->text() == QtGS(ACCEL_ASSIGN_BTN))
                {
                    B->click();
                }
            }
            CHECK(Page.EnableReset());
            Page.ApplySettings(true);
            CHECK(UISettingsLoadBool(Info_ShortCutsChanged));
            CHECK(UserKeys(ID_SYSTEM_PAUSE) == 1);
            Page.ResetPage();
            CHECK(!Page.EnableReset());
            Page.ApplySettings(true);
            CHECK(UserKeys(ID_SYSTEM_PAUSE) == 0);
            // Assign again and leave it saved for the "read" run (the reset
            // rebuilt the item tree)
            Items->setCurrentItem(FindPause());
            Keys->setCurrentIndex(F8);
            for (QPushButton * B : Page.findChildren<QPushButton *>())
            {
                if (B->text() == QtGS(ACCEL_ASSIGN_BTN))
                {
                    B->click();
                }
            }
            Page.ApplySettings(true);
            CHECK(UserKeys(ID_SYSTEM_PAUSE) == 1);
        }
    }

    // Leave the first two changes in place for the "read" run.
    return g_Failures;
}

static int DoRead()
{
    CHECK(g_Settings->LoadBool((SettingID)Setting_AutoFullscreen));
    CHECK(g_Settings->LoadDword((SettingID)Directory_RecentGameDirCount) == 7);
    QtGeneralOptionsPage General;
    CHECK(General.EnableReset());
    CHECK(CheckWithText(&General, OPTION_AUTO_FULLSCREEN)->isChecked());
    // The shortcut saved by the "write" run comes back from Project64.sc3
    {
        int Count = 0;
        CShortCuts Table;
        for (const CMenuShortCutKey & Key : Table.GetShortCuts().at(ID_SYSTEM_PAUSE).GetAccelItems())
        {
            Count += (Key.UserAdded() && Key.Key() == 0x77) ? 1 : 0;
        }
        CHECK(Count == 1);
        QtShortCutsPage Page;
        CHECK(Page.EnableReset());
    }
    return g_Failures;
}


// ---- every bound control of every page ----
static std::vector<QtSettingsPage *> AllPages()
{
    return {new QtGeneralOptionsPage, new QtAdvancedOptionsPage, new QtDefaultsOptionsPage, new QtPluginPage(false), new QtPluginPage(true),
            new QtDirectoriesPage, new QtDiskDrivePage, new QtGameGeneralPage, new QtGameRecompilerPage, new QtGameDiskDrivePage,
            new QtGameStatusPage, new QtGameBrowserPage};
}

static std::string SweepFile()
{
    return (std::filesystem::path(CPath::XdgDataHome()) / "settings-sweep.txt").string();
}

// What a control holds as a setting: the value of the item or the text
static bool ControlValue(const QtSettingsPage::BoundControl & C, std::string & Value)
{
    if (C.Type == QtSettingsPage::BoundControl::CheckBox)
    {
        Value = static_cast<QCheckBox *>(C.Widget)->isChecked() ? "1" : "0";
    }
    else if (C.Type == QtSettingsPage::BoundControl::TextBox)
    {
        Value = static_cast<QLineEdit *>(C.Widget)->text().toStdString();
    }
    else
    {
        QComboBox * Combo = static_cast<QComboBox *>(C.Widget);
        if (Combo->currentIndex() < 0)
        {
            return false;
        }
        Value = C.Type == QtSettingsPage::BoundControl::ComboBox ? std::to_string(Combo->currentData().toUInt()) : Combo->currentData().toString().toStdString();
    }
    return true;
}

// What the setting holds
static std::string SettingValue(const QtSettingsPage::BoundControl & C)
{
    switch (C.Type)
    {
    case QtSettingsPage::BoundControl::CheckBox: return g_Settings->LoadBool(C.Id) ? "1" : "0";
    case QtSettingsPage::BoundControl::ComboBox: return std::to_string(g_Settings->LoadDword(C.Id));
    case QtSettingsPage::BoundControl::ComboBoxTxt: return g_Settings->LoadStringVal(C.Id);
    default: return C.IsString ? g_Settings->LoadStringVal(C.Id) : std::to_string(g_Settings->LoadDword(C.Id));
    }
}

// The Game Status page edits the entry of the game in the database (the copy next to the test)
static void SweepGame()
{
    g_Settings->SaveBool(Setting_RdbEditor, true);
    g_Settings->SaveString(Game_IniKey, "635A2BFF-8B022326-C:45");
}

static int DoSweepWrite()
{
    SweepGame();
    // a confirmation (a message box) decides these: the test of them is by hand
    const std::set<int> Confirmed = {(int)Plugin_UseHleGfx, (int)Plugin_UseHleAudio, (int)Game_UseHleGfx, (int)Game_UseHleAudio};
    std::ofstream Manifest(SweepFile());
    int Controls = 0, Skipped = 0;
    for (QtSettingsPage * Page : AllPages())
    {
        const std::vector<QtSettingsPage::BoundControl> List = Page->BoundControls();
        for (const auto & C : List)
        {
            if (Confirmed.count((int)C.Id) != 0 || !C.Widget->isEnabledTo(Page))
            {
                Skipped++;
                continue;
            }
            std::string Before;
            if (!ControlValue(C, Before))
            {
                Skipped++;
                continue;
            }
            // the next value of the control, as a user does it
            if (C.Type == QtSettingsPage::BoundControl::CheckBox)
            {
                static_cast<QCheckBox *>(C.Widget)->click();
            }
            else if (C.Type == QtSettingsPage::BoundControl::TextBox)
            {
                QLineEdit * Edit = static_cast<QLineEdit *>(C.Widget);
                const QString Text = C.IsString ? QString("/sweep/%1").arg((int)C.Id) : QString::number(Before == "3" ? 4 : 3);
                Edit->setText(Text);
                Q_EMIT Edit->textEdited(Text);
            }
            else
            {
                QComboBox * Combo = static_cast<QComboBox *>(C.Widget);
                if (Combo->count() < 2)
                {
                    Skipped++;
                    continue;
                }
                const int Next = (Combo->currentIndex() + 1) % Combo->count();
                Combo->setCurrentIndex(Next);
                Q_EMIT Combo->activated(Next);
            }
            std::string After;
            ControlValue(C, After);
            if (After == Before && C.Type != QtSettingsPage::BoundControl::TextBox)
            {
                printf("FAIL control of setting %d did not change\n", (int)C.Id);
                g_Failures++;
                continue;
            }
            Page->ApplySettings(true);
            const std::string Stored = SettingValue(C);
            if (Stored != After)
            {
                printf("FAIL setting %d: control %s, setting %s\n", (int)C.Id, After.c_str(), Stored.c_str());
                g_Failures++;
            }
            Manifest << (int)C.Id << '\t' << (int)C.Type << '\t' << After << '\n';
            Controls++;
        }
        delete Page;
    }
    printf("sweep: %d controls changed, applied and checked, %d not (disabled, one item, confirmation)\n", Controls, Skipped);
    CHECK(Controls > 60);
    return g_Failures;
}

static int DoSweepRead()
{
    SweepGame();
    std::ifstream Manifest(SweepFile());
    std::map<int, std::pair<int, std::string>> Expected;
    std::string Line;
    while (std::getline(Manifest, Line))
    {
        std::istringstream Row(Line);
        int Id, Type;
        std::string Value;
        Row >> Id >> Type;
        Row.ignore(1);
        std::getline(Row, Value);
        Expected[Id] = {Type, Value};
    }
    int Checked = 0;
    for (QtSettingsPage * Page : AllPages())
    {
        for (const auto & C : Page->BoundControls())
        {
            auto Found = Expected.find((int)C.Id);
            if (Found == Expected.end())
            {
                continue;
            }
            std::string Shown;
            if (!ControlValue(C, Shown) || Shown != Found->second.second || SettingValue(C) != Found->second.second)
            {
                printf("FAIL setting %d after a restart: control %s, setting %s, written %s\n", (int)C.Id, Shown.c_str(), SettingValue(C).c_str(), Found->second.second.c_str());
                g_Failures++;
            }
            Checked++;
        }
        delete Page;
    }
    printf("sweep: %d controls show what was written\n", Checked);
    CHECK(Checked >= (int)Expected.size());
    return g_Failures;
}

// settings-test dump: every control of every page (class, text, items, bound setting), for the comparison
// with the dialogs of upstream
static void DumpWidget(QWidget * W, int Depth)
{
    for (QObject * O : W->children())
    {
        QWidget * C = qobject_cast<QWidget *>(O);
        if (C == nullptr)
        {
            continue;
        }
        QString Text, Extra;
        if (auto * B = qobject_cast<QAbstractButton *>(C))
        {
            Text = B->text();
        }
        else if (auto * L = qobject_cast<QLabel *>(C))
        {
            Text = L->text();
        }
        else if (auto * G = qobject_cast<QGroupBox *>(C))
        {
            Text = G->title();
        }
        else if (auto * Co = qobject_cast<QComboBox *>(C))
        {
            QStringList Items;
            for (int i = 0; i < Co->count(); i++)
            {
                Items << Co->itemText(i);
            }
            Extra = "[" + Items.join(" | ") + "]";
        }
        else if (auto * E = qobject_cast<QLineEdit *>(C))
        {
            Text = E->text();
        }
        const char * Name = C->metaObject()->className();
        if (strcmp(Name, "QWidget") != 0 && strncmp(Name, "QScroll", 7) != 0 && strncmp(Name, "QAbstract", 9) != 0 && strcmp(Name, "QLineEdit") != 0 || !Text.isEmpty() || !Extra.isEmpty())
        {
            if (strncmp(Name, "QLineEdit", 9) != 0 || true)
            {
                printf("%*s%s '%s' %s%s\n", Depth * 2, "", Name, Text.toUtf8().constData(), Extra.toUtf8().constData(), C->isEnabledTo(W->window()) ? "" : " (disabled)");
            }
        }
        DumpWidget(C, Depth + 1);
    }
}

static int DoDump()
{
    SweepGame();
    for (QtSettingsPage * Page : AllPages())
    {
        printf("=== %s (%s)\n", Page->metaObject()->className(), QtGS(Page->PageTitle()).toUtf8().constData());
        DumpWidget(Page, 1);
        for (const auto & C : Page->BoundControls())
        {
            printf("  bound: setting %d kind %d\n", (int)C.Id, (int)C.Type);
        }
        delete Page;
    }
    return 0;
}

// settings-test shots <directory>: every page as an image (to look at them)
static int DoShots(const char * Dir)
{
    SweepGame();
    std::filesystem::create_directories(Dir);
    int N = 0;
    for (QtSettingsPage * Page : AllPages())
    {
        Page->resize(460, 400);
        Page->show();
        QApplication::processEvents();
        Page->grab().save(QString("%1/%2-%3.png").arg(Dir).arg(N++, 2, 10, QChar('0')).arg(Page->metaObject()->className()));
        delete Page;
    }
    SettingsDialog Dialog(false);
    Dialog.resize(760, 560);
    Dialog.show();
    QApplication::processEvents();
    Dialog.grab().save(QString("%1/dialog.png").arg(Dir));
    return 0;
}

static std::string ReadAll(const std::string & Path)
{
    std::ifstream File(Path, std::ios::binary);
    std::stringstream Content;
    Content << File.rdbuf();
    return Content.str();
}

// The Game Status editor writes into the database. A read-only installation (the RDB next to the
// program cannot be written: AppImage, Flatpak, /usr) keeps its file untouched; the edits go to a copy
// under XDG_CONFIG_HOME/vortex64/Config, that is used from then on
static int DoRdb()
{
    namespace fs = std::filesystem;
    const fs::path Shipped = fs::path(CPath::XdgDataHome()) / "rdb-test-install";
    std::error_code Error;
    fs::create_directories(Shipped, Error);
    const std::string File = (Shipped / "Project64.rdb").string();
    const std::string Original = "[AAAAAAAA-BBBBBBBB-C:45]\nGood Name=Test Game\nStatus=Compatible\n";
    {
        std::ofstream Out(File, std::ios::binary);
        Out << Original;
    }
    chmod(File.c_str(), 0444);
    chmod(Shipped.string().c_str(), 0555);

    // the editor is off: the installed file is read as it is
    g_Settings->SaveBool(Setting_RdbEditor, false);
    CHECK(CSettingTypeRomDatabase::DatabasePath(File) == File);

    // the editor is on and the file cannot be written: a user copy
    g_Settings->SaveBool(Setting_RdbEditor, true);
    const std::string User = CSettingTypeRomDatabase::DatabasePath(File);
    CHECK(User != File);
    CHECK(User == (fs::path(CPath::XdgConfigHome()) / "vortex64" / "Config" / "Project64.rdb").string());
    CHECK(ReadAll(User) == Original);

    // an edit lands in the copy
    {
        CIniFile Ini(User.c_str());
        Ini.SaveString("AAAAAAAA-BBBBBBBB-C:45", "Status", "Stable");
        Ini.SaveString("AAAAAAAA-BBBBBBBB-C:45", "Culling", "1");
    }
    CHECK(ReadAll(File) == Original);
    // and it is still the file in use when the database is opened again
    CHECK(CSettingTypeRomDatabase::DatabasePath(File) == User);
    {
        CIniFile Ini(CSettingTypeRomDatabase::DatabasePath(File).c_str());
        CHECK(Ini.GetString("AAAAAAAA-BBBBBBBB-C:45", "Status", "") == "Stable");
        CHECK(Ini.GetString("AAAAAAAA-BBBBBBBB-C:45", "Culling", "") == "1");
        CHECK(Ini.GetString("AAAAAAAA-BBBBBBBB-C:45", "Good Name", "") == "Test Game");
    }
    CHECK(ReadAll(File) == Original);

    // a copy that an older version left read only is made writable again
    chmod(User.c_str(), 0444);
    CHECK(CSettingTypeRomDatabase::DatabasePath(File) == User);
    CHECK(access(User.c_str(), W_OK) == 0);

    // a writable installation (a development tree) is edited in place
    chmod(Shipped.string().c_str(), 0755);
    chmod(File.c_str(), 0644);
    const fs::path Writable = fs::path(CPath::XdgDataHome()) / "rdb-test-writable";
    fs::create_directories(Writable, Error);
    {
        std::ofstream Out((Writable / "Project64.rdb").string(), std::ios::binary);
        Out << Original;
    }
    CHECK(CSettingTypeRomDatabase::DatabasePath((Writable / "Project64.rdb").string()) == (Writable / "Project64.rdb").string());
    return g_Failures;
}

// ---------------------------------------------------------------------------------------------------------------------
// The way a game is found in Config/Project64.rdb: the key is CRC1-CRC2-C:country (the header, whatever the order of the bytes of the
// file), and what the database says for it - the memory, the system - is what the core uses. The expected values come from a plain
// reading of the shipped database (not from the core), the real values from the core.
struct RdbCase
{
    std::string Name;
    uint32_t Crc1, Crc2;
    uint8_t Country;
    bool InDatabase;
    int RdramMb; // 0: the entry does not say
};

static std::map<std::string, std::map<std::string, std::string>> ReadRdbText(const std::string & Path)
{
    std::map<std::string, std::map<std::string, std::string>> Db;
    std::ifstream In(Path);
    std::string Line, Section;
    while (std::getline(In, Line))
    {
        while (!Line.empty() && (Line.back() == '\r' || Line.back() == '\n'))
        {
            Line.pop_back();
        }
        if (Line.size() > 2 && Line.front() == '[' && Line.back() == ']')
        {
            Section = Line.substr(1, Line.size() - 2);
            Db[Section];
        }
        else if (!Section.empty() && Line.find('=') != std::string::npos)
        {
            Db[Section][Line.substr(0, Line.find('='))] = Line.substr(Line.find('=') + 1);
        }
    }
    return Db;
}

// a header (and a megabyte of nothing) in the order of the bytes of a file of the console: 0 z64 (big endian), 1 v64 (16 bit swapped), 2 n64 (32 bit swapped)
static std::string MakeRomFile(const std::string & Dir, const RdbCase & C, int Order)
{
    std::vector<uint8_t> Image(1 << 20, 0);
    const uint8_t Magic[4] = {0x80, 0x37, 0x12, 0x40};
    memcpy(Image.data(), Magic, 4);
    Image[8] = 0x80; Image[9] = 0x24; Image[10] = 0x60; Image[11] = 0x00;
    for (int i = 0; i < 4; i++)
    {
        Image[0x10 + i] = (C.Crc1 >> (24 - 8 * i)) & 0xFF;
        Image[0x14 + i] = (C.Crc2 >> (24 - 8 * i)) & 0xFF;
    }
    memset(&Image[0x20], ' ', 20);
    memcpy(&Image[0x20], C.Name.data(), std::min<size_t>(20, C.Name.size()));
    Image[0x3B] = 'N';
    Image[0x3C] = 'X';
    Image[0x3D] = 'X';
    Image[0x3E] = C.Country;
    if (Order == 1)
    {
        for (size_t i = 0; i + 1 < Image.size(); i += 2) std::swap(Image[i], Image[i + 1]);
    }
    else if (Order == 2)
    {
        for (size_t i = 0; i + 3 < Image.size(); i += 4) { std::swap(Image[i], Image[i + 3]); std::swap(Image[i + 1], Image[i + 2]); }
    }
    const std::string Path = Dir + "/rom" + std::to_string(Order) + ".bin";
    std::ofstream(Path, std::ios::binary).write(reinterpret_cast<const char *>(Image.data()), (std::streamsize)Image.size());
    return Path;
}

static int DoLookup()
{
    const std::string Rdb = CPath(CPath::MODULE_DIRECTORY).GetDirectory() + "/Config/Project64.rdb";
    const auto Db = ReadRdbText(Rdb);
    CHECK(Db.size() > 1000);
    std::vector<RdbCase> Cases;
    char Key[40];
    // Super Mario 64 of three countries: found by reading the database
    for (const auto & [Section, Entries] : Db)
    {
        const auto Name = Entries.find("Internal Name");
        if (Name == Entries.end() || Name->second != "SUPER MARIO 64" || Section.size() < 22)
        {
            continue;
        }
        const std::string Country = Section.substr(Section.size() - 2);
        if (Country != "45" && Country != "50" && Country != "4A")
        {
            continue;
        }
        bool Seen = false;
        for (const RdbCase & C : Cases)
        {
            Seen = Seen || C.Country == (uint8_t)std::stoul(Country, nullptr, 16);
        }
        if (Seen)
        {
            continue;
        }
        const auto Size = Entries.find("RDRAM Size");
        Cases.push_back({"SM64 " + Country, (uint32_t)std::stoul(Section.substr(0, 8), nullptr, 16), (uint32_t)std::stoul(Section.substr(9, 8), nullptr, 16), (uint8_t)std::stoul(Country, nullptr, 16), true, Size == Entries.end() ? 0 : atoi(Size->second.c_str())});
    }
    CHECK(Cases.size() == 3); // USA, Europe and Japan are in the database
    // games that are not in it: a hack of each country (the key has the country)
    for (uint8_t Country : {(uint8_t)0x45, (uint8_t)0x50, (uint8_t)0x4A})
    {
        snprintf(Key, sizeof(Key), "%08X-%08X-C:%X", 0x1BADF00Du, 0x0DDBA11Eu, Country);
        CHECK(Db.find(Key) == Db.end());
        Cases.push_back({std::string("unknown ") + (Country == 0x45 ? "USA" : Country == 0x50 ? "EUR" : "JPN"), 0x1BADF00D, 0x0DDBA11E, Country, false, 0});
    }
    const std::string Dir = std::string(CPath::XdgDataHome()) + "/rdb-lookup";
    std::filesystem::create_directories(Dir);
    int Checked = 0;
    for (const RdbCase & C : Cases)
    {
        for (int Order = 0; Order < 3; Order++)
        {
            const std::string File = MakeRomFile(Dir, C, Order);
            CN64Rom Rom;
            CHECK(Rom.LoadN64Image(File.c_str(), true));
            Rom.SaveRomSettingID(true);
            snprintf(Key, sizeof(Key), "%08X-%08X-C:%X", C.Crc1, C.Crc2, C.Country);
            const std::string Ident = g_Settings->LoadStringVal(Game_IniKey);
            const bool Known = g_Settings->LoadBool(Game_Known);
            uint32_t Size = 0;
            const bool RdbSets = g_Settings->LoadDword(Rdb_RDRamSize, Size);
            // what the core allocates (CMipsMemoryVM::AllocateMemory)
            uint32_t Allocated = 0;
            bool AnyRdb = g_Settings->LoadDword(Rdb_RDRamSize, Allocated);
            if (!g_Settings->LoadDword(Game_RDRamSize, Allocated) && !AnyRdb)
            {
                Allocated = g_Settings->LoadDword(Known ? Default_RDRamSizeKnown : Default_RDRamSizeUnknown);
            }
            const uint32_t Expected = C.RdramMb != 0 ? (C.RdramMb == 8 ? 0x800000u : 0x400000u) : (C.InDatabase ? 0x400000u : 0x800000u);
            const bool Pal = g_Settings->LoadDword(Game_SystemType) == SYSTEM_PAL;
            printf("lookup %s order %d: key %s, in database %d, RDRAM size in the entry %d, the core allocates %u MB (expected %u), %s\n", C.Name.c_str(), Order, Ident.c_str(), (int)Known, (int)RdbSets, Allocated >> 20, Expected >> 20, Pal ? "PAL" : "NTSC");
            CHECK(Ident == Key);
            CHECK(Known == C.InDatabase);
            CHECK(RdbSets == (C.RdramMb != 0));
            CHECK(Allocated == Expected);
            CHECK(Pal == (C.Country == 0x50));
            Checked++;
        }
    }
    printf("lookup: %d headers checked\n", Checked);
    return g_Failures == 0 ? 0 : 1;
}

int main(int argc, char ** argv)
{
    if (argc < 2 || (strcmp(argv[1], "write") != 0 && strcmp(argv[1], "read") != 0 && strcmp(argv[1], "rdb") != 0 && strcmp(argv[1], "lookup") != 0 && strcmp(argv[1], "sweep-write") != 0 && strcmp(argv[1], "dump") != 0 && strcmp(argv[1], "shots") != 0 && strcmp(argv[1], "sweep-read") != 0))
    {
        printf("usage: %s write|read|rdb|sweep-write|sweep-read\n", argv[0]);
        return 2;
    }
    QApplication App(argc, argv);
    static CLinuxNotification Notify;
    const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
    if (!AppInit(&Notify, Base.c_str(), 1, argv))
    {
        printf("AppInit failed\n");
        return 2;
    }
    RegisterUISettings();

    int Result;
    if (strcmp(argv[1], "write") == 0)
    {
        Result = DoWrite();
    }
    else if (strcmp(argv[1], "rdb") == 0)
    {
        Result = DoRdb();
    }
    else if (strcmp(argv[1], "lookup") == 0)
    {
        Result = DoLookup();
    }
    else if (strcmp(argv[1], "shots") == 0)
    {
        Result = DoShots(argc > 2 ? argv[2] : "settings-shots");
    }
    else if (strcmp(argv[1], "dump") == 0)
    {
        Result = DoDump();
    }
    else if (strcmp(argv[1], "sweep-write") == 0)
    {
        Result = DoSweepWrite();
    }
    else if (strcmp(argv[1], "sweep-read") == 0)
    {
        Result = DoSweepRead();
    }
    else
    {
        Result = DoRead();
    }
    AppCleanup();
    printf(Result == 0 ? "PASS %s\n" : "FAILED %s\n", argv[1]);
    return Result == 0 ? 0 : 1;
}
