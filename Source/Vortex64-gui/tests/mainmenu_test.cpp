// Regression test for the Qt main menu (Source/Vortex64-gui/MainMenu.cpp, the
// port of CMainMenu): menu structure for the "no game" and "game running"
// states, enable/tick rules, shortcut installation from the CShortCuts table
// and the settings a few commands change. Offscreen Qt, isolated XDG dirs.
#include "../MainMenu.h"
#include "../Vortex/Language.h"
#include "../Settings/SettingsDialog.h"
#include "../Settings/VortexPages.h"
#include "../MainWindow.h"
#include "../Dialogs/EnhancementTree.h"
#include "../Dialogs/LogOptionsDialog.h"
#include "../Dialogs/FirstRun.h"

#include <Common/path.h>
#include <Vortex64-cli/LinuxNotification.h>
#include <Vortex64-core/AppInit.h>
#include <Vortex64-core/Multilanguage.h>
#include <Vortex64-core/Settings.h>
#include <Vortex64-core/Settings/LoggingSettings.h>
#include <Vortex64-ui/Settings/UISettings.h>

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <QSignalSpy>
#include <QTimer>
#include <cstdio>
#include <functional>

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

static QString Lang(LanguageStringID ID)
{
    return QString::fromUtf8(GS(ID));
}

// The action with that menu ID, in any menu or submenu
static QAction * FindIn(QMenu * Menu, int Id)
{
    for (QAction * Action : Menu->actions())
    {
        if (Action->property("menuId").isValid() && Action->property("menuId").toInt() == Id)
        {
            return Action;
        }
        if (Action->menu() != nullptr)
        {
            if (QAction * Found = FindIn(Action->menu(), Id))
            {
                return Found;
            }
        }
    }
    return nullptr;
}

static QAction * FindId(MainWindow & Window, int Id)
{
    for (QAction * Top : Window.menuBar()->actions())
    {
        if (Top->menu() != nullptr)
        {
            if (QAction * Found = FindIn(Top->menu(), Id))
            {
                return Found;
            }
        }
    }
    return nullptr;
}

static QMenu * TopMenu(MainWindow & Window, LanguageStringID Title)
{
    for (QAction * Action : Window.menuBar()->actions())
    {
        if (Action->menu() != nullptr && Action->text() == Lang(Title))
        {
            return Action->menu();
        }
    }
    return nullptr;
}

static QAction * Find(QMenu * Menu, LanguageStringID Text)
{
    if (Menu == nullptr)
    {
        return nullptr;
    }
    for (QAction * Action : Menu->actions())
    {
        if (Action->text() == Lang(Text))
        {
            return Action;
        }
    }
    return nullptr;
}

int main(int argc, char ** argv)
{
    QApplication App(argc, argv);
    static CLinuxNotification Notify;
    const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
    if (!AppInit(&Notify, Base.c_str(), 1, argv))
    {
        return 2;
    }
    RegisterUISettings();
    g_Settings->SaveBool(UserInterface_BasicMode, false);
    g_Settings->SaveBool(GameRunning_CPU_Running, false);

    MainWindow Window;
    QtMainMenu Menu(&Window);

    // No game: File, View, Settings, Tools and Help (Emulation needs a running game)
    auto TopCount = [&]() { return Window.menuBar()->actions().size(); };
    CHECK(TopCount() == 5);
    CHECK(FindId(Window, ID_FILE_OPEN_ROM) != nullptr && FindId(Window, ID_FILE_OPEN_ROM)->isEnabled());
    // The default key table gives Open ROM Ctrl+O while no game runs
    CHECK(FindId(Window, ID_FILE_OPEN_ROM) != nullptr && FindId(Window, ID_FILE_OPEN_ROM)->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_O)));
    CHECK(FindId(Window, ID_FILE_OPEN_COMBO) != nullptr);
    CHECK(FindId(Window, ID_FILE_ROM_INFO) != nullptr && !FindId(Window, ID_FILE_ROM_INFO)->isEnabled());
    CHECK(FindId(Window, ID_FILE_ENDEMULATION) != nullptr && !FindId(Window, ID_FILE_ENDEMULATION)->isEnabled());
    CHECK(FindId(Window, ID_FILE_EXIT) != nullptr);
    // (the language, the ROM folder and the support page moved to the settings window and the library)
    CHECK(FindId(Window, ID_LANG_START) == nullptr);
    CHECK(FindId(Window, ID_HELP_SUPPORT_PROJECT64) == nullptr);
    CHECK(FindId(Window, ID_HELP_ABOUT) != nullptr);

    // Settings: the window and the plugin windows (disabled without plugins)
    CHECK(FindId(Window, ID_OPTIONS_SETTINGS) != nullptr && FindId(Window, ID_OPTIONS_SETTINGS)->isEnabled());
    CHECK(FindId(Window, ID_OPTIONS_FULLSCREEN) != nullptr && !FindId(Window, ID_OPTIONS_FULLSCREEN)->isEnabled());
    CHECK(FindId(Window, ID_SYSTEM_PAUSE) == nullptr);

    // A running game adds the Emulation menu with the upstream items
    g_Settings->SaveBool(GameRunning_CPU_Running, true);
    Menu.ResetMenu();
    CHECK(TopCount() == 6);
    CHECK(FindId(Window, ID_SYSTEM_PAUSE) != nullptr);
    CHECK(FindId(Window, ID_SYSTEM_PAUSE) != nullptr && FindId(Window, ID_SYSTEM_PAUSE)->shortcuts().contains(QKeySequence(Qt::Key_F2)));
    CHECK(FindId(Window, ID_SYSTEM_PAUSE) != nullptr && FindId(Window, ID_SYSTEM_PAUSE)->shortcuts().contains(QKeySequence(Qt::Key_Pause)));
    CHECK(FindId(Window, ID_SYSTEM_SAVE) != nullptr && FindId(Window, ID_SYSTEM_SAVE)->shortcuts().contains(QKeySequence(Qt::Key_F5)));
    CHECK(FindId(Window, ID_SYSTEM_RESTORE) != nullptr && FindId(Window, ID_SYSTEM_RESTORE)->shortcuts().contains(QKeySequence(Qt::Key_F7)));
    CHECK(FindId(Window, ID_SYSTEM_CHEAT) != nullptr);
    CHECK(FindId(Window, ID_SYSTEM_GSBUTTON) != nullptr);
    CHECK(FindId(Window, ID_SYSTEM_SWAPDISK) != nullptr && !FindId(Window, ID_SYSTEM_SWAPDISK)->isEnabled()); // no 64DD disk
    CHECK(FindId(Window, ID_SYSTEM_LIMITFPS) != nullptr && FindId(Window, ID_SYSTEM_LIMITFPS)->isCheckable());
    CHECK(FindId(Window, ID_FILE_ENDEMULATION)->isEnabled());
    // While a game runs Open ROM is bound to the running-state entry
    CHECK(FindId(Window, ID_FILE_OPEN_ROM)->shortcuts().contains(QKeySequence(Qt::CTRL | Qt::Key_O)));
    // Save slots: 11 entries (default + 1..10)
    {
        int Slots = 0;
        for (int Slot = ID_CURRENT_SAVE_1; Slot <= ID_CURRENT_SAVE_10; Slot++)
        {
            Slots += FindId(Window, Slot) != nullptr ? 1 : 0;
        }
        Slots += FindId(Window, ID_CURRENT_SAVE_DEFAULT) != nullptr ? 1 : 0;
        CHECK(Slots == 11);
    }

    // Commands that only touch settings
    g_Settings->SaveBool(GameRunning_LimitFPS, false);
    CHECK(Menu.ProcessMessage(ID_SYSTEM_LIMITFPS));
    CHECK(g_Settings->LoadBool(GameRunning_LimitFPS));
    CHECK(Menu.ProcessMessage(ID_SYSTEM_LIMITFPS));
    CHECK(!g_Settings->LoadBool(GameRunning_LimitFPS));
    CHECK(Menu.ProcessMessage(ID_CURRENT_SAVE_3));
    CHECK(g_Settings->LoadDword(Game_CurrentSaveState) == 3);
    CHECK(Menu.ProcessMessage(ID_CURRENT_SAVE_DEFAULT));
    CHECK(g_Settings->LoadDword(Game_CurrentSaveState) == 0);
    CHECK(Menu.ProcessMessage(ID_CURRENT_SAVE_10));
    CHECK(g_Settings->LoadDword(Game_CurrentSaveState) == 10);
    g_Settings->SaveBool(UserInterface_DisplayFrameRate, false);
    CHECK(Menu.ProcessMessage(ID_OPTIONS_DISPLAY_FR));
    CHECK(g_Settings->LoadBool(UserInterface_DisplayFrameRate));
    g_Settings->SaveDword(UserInterface_FrameDisplayType, FR_VIs);
    CHECK(Menu.ProcessMessage(ID_OPTIONS_CHANGE_FR));
    CHECK(g_Settings->LoadDword(UserInterface_FrameDisplayType) == FR_DLs);
    CHECK(Menu.ProcessMessage(ID_OPTIONS_CHANGE_FR));
    CHECK(g_Settings->LoadDword(UserInterface_FrameDisplayType) == FR_PERCENT);
    CHECK(Menu.ProcessMessage(ID_OPTIONS_CHANGE_FR));
    CHECK(g_Settings->LoadDword(UserInterface_FrameDisplayType) == FR_VIs);
    CHECK(!Menu.ProcessMessage(0)); // unknown id

    // Virtual key translation used for the Project64.sc3 key table
    CHECK(QtMainMenu::QtKeyFromVirtualKey('A') == Qt::Key_A);
    CHECK(QtMainMenu::QtKeyFromVirtualKey(0x70) == Qt::Key_F1);
    CHECK(QtMainMenu::QtKeyFromVirtualKey(0x7B) == Qt::Key_F12);
    CHECK(QtMainMenu::QtKeyFromVirtualKey(0x0D) == Qt::Key_Return);
    CHECK(QtMainMenu::QtKeyFromVirtualKey(0xBB) == Qt::Key_Equal);

    // Basic mode hides the entries upstream hides
    g_Settings->SaveBool(UserInterface_BasicMode, true);
    Menu.ResetMenu();
    CHECK(FindId(Window, ID_SYSTEM_LIMITFPS) == nullptr);
    CHECK(FindId(Window, ID_SYSTEM_SAVEAS) == nullptr);
    CHECK(FindId(Window, ID_OPTIONS_CPU_USAGE) == nullptr);

    // Cheat/enhancement check box tree (CCheatList): nesting by '\\', tri-state parents,
    // code extensions must be chosen before a code can be switched on
    {
        CEnhancementList List;
        auto AddCheat = [&](const char * Name, bool WithOptions) {
            CEnhancement Cheat(CEnhancement::CheatIdent);
            Cheat.SetName(Name);
            CEnhancement::CodeEntries Entries;
            CEnhancement::CodeEntry Entry;
            Entry.Command = 0x8033B21E;
            Entry.Value = WithOptions ? "????" : "0063";
            Entries.push_back(Entry);
            Cheat.SetEntries(Entries);
            if (WithOptions)
            {
                CEnhancement::CodeOptions Options;
                Options.push_back({"One", 1});
                Options.push_back({"Two", 2});
                Cheat.SetOptions(Options);
            }
            List.AddItem(Cheat);
        };
        AddCheat("Group\\Alpha", false);
        AddCheat("Group\\Beta", false);
        AddCheat("Solo", false);
        AddCheat("WithOption", true);
        EnhancementTree Tree;
        Tree.Refresh(List);
        CHECK(Tree.topLevelItemCount() == 3);
        QTreeWidgetItem * Group = nullptr;
        QTreeWidgetItem * Solo = nullptr;
        QTreeWidgetItem * WithOption = nullptr;
        for (int i = 0; i < Tree.topLevelItemCount(); i++)
        {
            QTreeWidgetItem * Item = Tree.topLevelItem(i);
            if (Item->text(0) == "Group")
            {
                Group = Item;
            }
            else if (Item->text(0) == "Solo")
            {
                Solo = Item;
            }
            else
            {
                WithOption = Item;
            }
        }
        CHECK(Group != nullptr && Group->childCount() == 2);
        CHECK(Solo != nullptr && WithOption != nullptr);
        if (Group != nullptr && Group->childCount() == 2 && Solo != nullptr && WithOption != nullptr)
        {
            QSignalSpy Active(&Tree, &EnhancementTree::ActiveChanged);
            QSignalSpy Choose(&Tree, &EnhancementTree::ChooseOption);
            QTreeWidgetItem * Alpha = Group->child(0)->text(0) == "Alpha" ? Group->child(0) : Group->child(1);
            QTreeWidgetItem * Beta = Group->child(0)->text(0) == "Alpha" ? Group->child(1) : Group->child(0);
            Alpha->setCheckState(0, Qt::Checked);
            CHECK(Tree.EnhancementOf(Alpha)->Active());
            CHECK(!Tree.EnhancementOf(Beta)->Active());
            CHECK(Group->checkState(0) == Qt::PartiallyChecked);
            Beta->setCheckState(0, Qt::Checked);
            CHECK(Group->checkState(0) == Qt::Checked);
            CHECK(Active.count() == 2);
            // Checking a parent switches every child on and off
            Group->setCheckState(0, Qt::Unchecked);
            CHECK(!Tree.EnhancementOf(Alpha)->Active() && !Tree.EnhancementOf(Beta)->Active());
            Group->setCheckState(0, Qt::Checked);
            CHECK(Tree.EnhancementOf(Alpha)->Active() && Tree.EnhancementOf(Beta)->Active());
            // A code with extensions asks for a value first and stays off
            WithOption->setCheckState(0, Qt::Checked);
            CHECK(Choose.count() == 1);
            CHECK(WithOption->checkState(0) == Qt::Unchecked);
            CHECK(!Tree.EnhancementOf(WithOption)->Active());
            // Mark none
            Tree.SetAllChecked(false);
            CHECK(!Tree.EnhancementOf(Alpha)->Active() && !Tree.EnhancementOf(Beta)->Active());
            CHECK(Group->checkState(0) == Qt::Unchecked);
        }
    }

    // First run: nothing is asked; Spanish and GLideN64 (when installed) are saved
    {
        g_Settings->SaveString(Plugin_GFX_Current, "something-else.so");
        ApplyFirstRunDefaults(&Window);
        CHECK(g_Settings->LoadStringVal(Setting_CurrentLanguage) == "Español");
        CHECK(QApplication::activeModalWidget() == nullptr);
        CHECK(g_Settings->LoadStringVal(Plugin_GFX_Current) ==
              (QFile::exists(QCoreApplication::applicationDirPath() + "/libGLideN64.so") ? "libGLideN64.so" : "something-else.so"));
        Vortex::ApplyLanguage("en");
    }

    // The language is chosen in the settings window (the General category), not in a menu
    {
        SettingsDialog Dialog(false);
        Vortex::LanguagePage * Page = nullptr;
        for (QWidget * Child : Dialog.findChildren<QWidget *>())
        {
            if (Vortex::LanguagePage * Found = qobject_cast<Vortex::LanguagePage *>(Child))
            {
                Page = Found;
            }
        }
        CHECK(Page != nullptr);
        if (Page != nullptr)
        {
            const int Index = Page->Combo()->findData("es");
            CHECK(Index >= 0);
            if (Index >= 0)
            {
                Page->Combo()->setCurrentIndex(Index);
                QMetaObject::invokeMethod(Page->Combo(), "activated", Q_ARG(int, Index));
                Page->ApplySettings(false);
                CHECK(g_Settings->LoadStringVal(Setting_CurrentLanguage) == "Español");
                Vortex::ApplyLanguage("en"); // (the texts of the rest of the test are English)
            }
        }
    }

    // Debugger > Logging > Log options...: the page texts of upstream's property sheet, the
    // state of the boxes from the log settings and the Logging_* settings written by OK
    {
        CHECK(g_Settings->LoadBool(Logging_LogExceptions) == false);
        g_Settings->SaveBool(Logging_LogTLB, true);
        g_Settings->SaveBool(Logging_LogSPRegisters, true);
        LogOptionsDialog Dialog;
        QCheckBox * Tlb = Dialog.Find("Include TLB modifications");
        QCheckBox * Exceptions = Dialog.Find("Include Exceptions");
        QCheckBox * Sp = Dialog.Find("Include SP Registers accesses");
        QCheckBox * DirectWrite = Dialog.Find("Include direct writing to Pif Ram");
        QCheckBox * Interrupts = Dialog.Find("Do not log interrupts");
        CHECK(Tlb && Exceptions && Sp && DirectWrite && Interrupts);
        if (Tlb && Exceptions && Sp && DirectWrite && Interrupts)
        {
            CHECK(Tlb->isChecked());
            CHECK(Sp->isChecked());
            CHECK(!Exceptions->isChecked());
            Exceptions->setChecked(true);
            Tlb->setChecked(false);
            DirectWrite->setChecked(true);
            // nothing is written until OK
            CHECK(!g_Settings->LoadBool(Logging_LogExceptions));
            Dialog.Apply();
            CHECK(g_Settings->LoadBool(Logging_LogExceptions));
            CHECK(!g_Settings->LoadBool(Logging_LogTLB));
            CHECK(g_Settings->LoadBool(Logging_LogSPRegisters));
            // upstream pairs "direct writing to Pif Ram" with Logging_LogPRDirectMemLoads
            CHECK(g_Settings->LoadBool(Logging_LogPRDirectMemLoads));
            CHECK(!g_Settings->LoadBool(Logging_LogPRDirectMemStores));
            CHECK(g_LogSettings.logExceptions && !g_LogSettings.logTlb);
            // and reopened it shows what was saved
            LogOptionsDialog Again;
            CHECK(Again.Find("Include Exceptions")->isChecked());
            CHECK(!Again.Find("Include TLB modifications")->isChecked());
            Exceptions->setChecked(false);
            Dialog.Apply();
        }
        // the menu entry exists with the debugger enabled
        g_Settings->SaveBool(UserInterface_BasicMode, false);
        g_Settings->SaveBool(Debugger_Enabled, true);
        Menu.ResetMenu();
        std::function<bool(QMenu *)> Search = [&](QMenu * Menu) {
            for (QAction * Item : Menu->actions())
            {
                if (Item->text() == "Log options..." || (Item->menu() != nullptr && Search(Item->menu())))
                {
                    return true;
                }
            }
            return false;
        };
        bool Found = false;
        for (QAction * Top : Window.menuBar()->actions())
        {
            Found = Found || (Top->menu() != nullptr && Search(Top->menu()));
        }
        CHECK(Found);
    }

    AppCleanup();
    printf(g_Failures == 0 ? "PASS\n" : "FAILED\n");
    return g_Failures == 0 ? 0 : 1;
}
