// One key, one action; and the emulation stands still while a window of configuration is open.
//
//   input-capture-test write   (the bindings of Player 1: Escape -> Right, Space, Tab, F8 ... are buttons of the pad)
//   hotkey-pause-test <rom>
//
// A real game runs in the main window and the events are the ones of the X server (XTEST). Part 1: a key that is a button of the pad AND
// the shortcut of Pause (the user set it so) goes to the game only; a key that is only a shortcut is the shortcut's; a combination
// (Ctrl+key) is the shortcut's and not the pad's. Part 2: Settings, the dialogs of the plugins and the others pause the game, nested
// ones do not resume it under the dialog that stays, a game that the user paused stays paused, and every way of closing frees the pause.
#include "ui_test_env.h"
#include "../MainMenuID.h"

#include <Project64-core/N64System/ConfigPause.h>
#include <Project64-core/Plugins/ControllerPlugin.h>
#include <Project64-core/Plugins/Plugin.h>

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QPushButton>
#include <QTimer>
#include <X11/Xlib.h>
#undef KeyPress
#undef KeyRelease
#undef None
#undef Bool
#include <unistd.h>

extern "C" int XTestFakeKeyEvent(Display *, unsigned int, int, unsigned long);

static Display * g_X;
static MainWindow * g_Window;

static uint32_t Read()
{
    BUTTONS Keys = {};
    g_Plugins->Control()->GetKeys(0, &Keys);
    return Keys.Value;
}
static bool Running()
{
    return g_Settings->LoadBool(GameRunning_CPU_Running);
}
static bool Paused()
{
    return g_Settings->LoadBool(GameRunning_CPU_Paused);
}
static void Pump(int Ms)
{
    for (int i = 0; i < Ms / 10; i++)
    {
        QApplication::processEvents();
        usleep(10000);
    }
}
static void Key(unsigned Code, bool Down)
{
    XTestFakeKeyEvent(g_X, Code, Down ? 1 : 0, 0);
    XFlush(g_X);
}
static void Tap(unsigned Code, int HoldMs = 80)
{
    Key(Code, true);
    Pump(HoldMs);
    Key(Code, false);
    Pump(80);
}
static QAction * Action(int MenuId)
{
    for (QAction * A : g_Window->findChildren<QAction *>())
    {
        if (A->property("menuId").toInt() == MenuId)
        {
            return A;
        }
    }
    return nullptr;
}
// (the menu is made again when the state of the game changes: the actions are looked for every time, with the shortcuts of the user)
static void SetShortcut(int MenuId, const QKeySequence & Sequence)
{
    QAction * A = Action(MenuId);
    CHECK(A != nullptr);
    if (A != nullptr)
    {
        A->setShortcuts(Sequence.isEmpty() ? QList<QKeySequence>() : QList<QKeySequence>{Sequence});
    }
}
static void Settle()
{
    g_Window->activateWindow();
    g_Window->raise();
    Pump(300);
}
static QDialog * Find(const char * Name)
{
    for (QWidget * W : QApplication::topLevelWidgets())
    {
        QDialog * D = qobject_cast<QDialog *>(W);
        if (D != nullptr && D->isVisible() && (Name == nullptr || D->objectName() == Name))
        {
            return D;
        }
    }
    return nullptr;
}
static void ResumeIfPaused()
{
    if (Paused())
    {
        g_BaseSystem->ExternalEvent(SysEvent_ResumeCPU_FromMenu);
        WaitFor([]() { return !Paused(); }, 5000);
    }
}

enum
{
    X_ESC = 9,
    X_F2 = 68,
    X_CTRL_L = 37
};

static void PartKeys()
{
    printf("part 1: one key, one action\n");
    CHECK(Action(ID_SYSTEM_PAUSE) != nullptr && Action(ID_OPTIONS_FULLSCREEN) != nullptr);
    // the plugin knows which keys are buttons of the pad
    auto IsAssigned = reinterpret_cast<int (*)(int)>(g_Plugins->Control()->OptionalExport("VortexInputIsKeyAssigned"));
    CHECK(IsAssigned != nullptr);
    if (IsAssigned == nullptr)
    {
        return;
    }
    CHECK(IsAssigned(1) == 1);  // Escape
    SetShortcut(ID_OPTIONS_FULLSCREEN, QKeySequence());
    // 1. Escape is a button of the pad (-> Right) AND the shortcut of Pause: only the game gets it
    CHECK(IsAssigned(1) == 1);
    SetShortcut(ID_SYSTEM_PAUSE, QKeySequence(Qt::Key_Escape));
    Settle();
    CHECK(Running() && !Paused());
    Key(X_ESC, true);
    bool Reached = false;
    for (int i = 0; i < 40 && !Reached; i++)
    {
        Pump(20);
        Reached = (Read() & 1u) != 0;
    }
    Pump(200);
    printf("%s the key of the pad that is also Pause: pad %s, game %s\n", Reached && !Paused() ? "ok" : "BAD", Reached ? "got it" : "did not", Paused() ? "PAUSED" : "not paused");
    CHECK(Reached);
    CHECK(!Paused()); // no double execution
    Key(X_ESC, false);
    Pump(200);
    CHECK((Read() & 0xFFFF) == 0);
    // (held: the repetitions do not pause it either)
    Key(X_ESC, true);
    Pump(700);
    CHECK(!Paused());
    Key(X_ESC, false);
    Pump(200);
    // 2. the InputBridge keeps delivering the pad as before: the other buttons
    Key(50, true); // Left Shift -> Down
    bool Down = false;
    for (int i = 0; i < 40 && !Down; i++)
    {
        Pump(20);
        Down = (Read() & (1u << 2)) != 0;
    }
    Key(50, false);
    Pump(150);
    CHECK(Down);
    // 3. a key that is only a shortcut is the shortcut's: F2 pauses, and again resumes
    SetShortcut(ID_SYSTEM_PAUSE, QKeySequence(Qt::Key_F2));
    Settle();
    CHECK(IsAssigned(60) == 0);
    Tap(X_F2);
    const bool Stopped = WaitFor([]() { return Paused(); }, 5000);
    printf("%s a key that is only a shortcut pauses (%d)\n", Stopped ? "ok" : "BAD", (int)Stopped);
    CHECK(Stopped);
    Tap(X_F2);
    CHECK(WaitFor([]() { return !Paused(); }, 5000));
    // 4. a combination: Ctrl+Escape is Pause although Escape is a button; the pad does not get that Escape
    Settle();
    SetShortcut(ID_SYSTEM_PAUSE, QKeySequence(Qt::CTRL | Qt::Key_Escape));
    Settle();
    Key(X_CTRL_L, true);
    Pump(100);
    Key(X_ESC, true);
    Pump(150);
    const uint32_t WithCtrl = Read();
    const bool CombinationPaused = WaitFor([]() { return Paused(); }, 5000);
    Key(X_ESC, false);
    Key(X_CTRL_L, false);
    Pump(200);
    printf("%s Ctrl+Escape (Escape is a button): pauses %d, pad state %08x\n", CombinationPaused && (WithCtrl & 1u) == 0 ? "ok" : "BAD", (int)CombinationPaused, WithCtrl);
    CHECK(CombinationPaused);
    CHECK((WithCtrl & 1u) == 0);
    ResumeIfPaused();
    // 4b. Shift: a Shift that is itself a button of the pad (Left Shift -> Down) does not make Shift+Escape a combination; one that is not
    // (Right Shift) does
    Settle();
    SetShortcut(ID_SYSTEM_PAUSE, QKeySequence(Qt::SHIFT | Qt::Key_Escape));
    Key(50, true); // Left Shift: a button
    Pump(100);
    Key(X_ESC, true);
    Pump(400);
    const bool PausedByPadShift = Paused();
    const uint32_t PadState = Read();
    Key(X_ESC, false);
    Key(50, false);
    Pump(300);
    printf("%s Left Shift (a button) + Escape: not a shortcut (paused %d, pad %08x)\n", !PausedByPadShift && (PadState & 1u) && (PadState & 4u) ? "ok" : "BAD", (int)PausedByPadShift, PadState);
    CHECK(!PausedByPadShift && (PadState & 1u) && (PadState & 4u));
    ResumeIfPaused();
    Settle();
    SetShortcut(ID_SYSTEM_PAUSE, QKeySequence(Qt::SHIFT | Qt::Key_Escape));
    Key(62, true); // Right Shift: not a button
    Pump(100);
    Key(X_ESC, true);
    const bool PausedByRealShift = WaitFor([]() { return Paused(); }, 5000);
    Key(X_ESC, false);
    Key(62, false);
    Pump(300);
    printf("%s Right Shift (not a button) + Escape: the shortcut (paused %d)\n", PausedByRealShift ? "ok" : "BAD", (int)PausedByRealShift);
    CHECK(PausedByRealShift);
    ResumeIfPaused();
    // 5. the full screen shortcut (the combination of the window) still works when it is not a button of the pad
    Settle();
    SetShortcut(ID_OPTIONS_FULLSCREEN, QKeySequence(Qt::ALT | Qt::Key_Return));
    SetShortcut(ID_SYSTEM_PAUSE, QKeySequence(Qt::Key_F2));
    Settle();
    int Fired = 0;
    QObject::connect(Action(ID_OPTIONS_FULLSCREEN), &QAction::triggered, [&Fired]() { Fired++; });
    Key(64, true); // Alt_L
    Pump(100);
    Tap(36);
    Key(64, false);
    Pump(300);
    printf("%s Alt+Return reaches its shortcut (fired %d)\n", Fired > 0 ? "ok" : "BAD", Fired);
    CHECK(Fired > 0);
    if (g_Window->isFullScreen())
    {
        g_Window->showNormal();
        Pump(300);
    }
    ResumeIfPaused();
    Settle();
}

// Settings (and what it opens) while the game runs
static int g_Stage;
static void CloseWith(QDialog * Dialog, const char * How)
{
    if (strcmp(How, "x") == 0)
    {
        Dialog->close();
    }
    else if (strcmp(How, "cancel") == 0)
    {
        Dialog->findChild<QPushButton *>("VxCancel")->click();
    }
    else if (strcmp(How, "ok") == 0)
    {
        Dialog->findChild<QPushButton *>("VxOk")->click();
    }
    else if (strcmp(How, "apply-ok") == 0)
    {
        QPushButton * Apply = Dialog->findChild<QPushButton *>("VxApply");
        if (Apply != nullptr && Apply->isEnabled())
        {
            Apply->click();
        }
        Dialog->findChild<QPushButton *>("VxOk")->click();
    }
}

static void OpenSettingsAndClose(const char * How, bool UserPausedFirst)
{
    Settle();
    ResumeIfPaused();
    CHECK(Running());
    if (UserPausedFirst)
    {
        g_BaseSystem->ExternalEvent(SysEvent_PauseCPU_FromMenu);
        CHECK(WaitFor([]() { return Paused(); }, 5000));
    }
    bool WasPausedInside = false;
    int Depth = -1;
    QTimer::singleShot(600, [&]() {
        QDialog * Dialog = Find("SettingsDialog");
        if (Dialog == nullptr)
        {
            Dialog = Find(nullptr);
        }
        Pump(300);
        WasPausedInside = Paused();
        Depth = ConfigPause::OpenCount();
        // the key that is a button of the pad does not reach the game while the dialog is open
        Key(X_ESC, true);
        Pump(300);
        const uint32_t State = Read();
        Key(X_ESC, false);
        Pump(100);
        CHECK((State & 0xFFFF) == 0);
        CHECK(Dialog != nullptr);
        if (Dialog != nullptr)
        {
            CloseWith(Dialog, How);
        }
    });
    SettingsDialog Dialog(false, g_Window);
    Dialog.exec();
    g_Window->activateWindow(); // (the window manager gives the focus back to the window of the game; this server has none)
    const bool Back = UserPausedFirst ? true : WaitFor([]() { return !Paused(); }, 5000);
    Pump(300);
    printf("   (pause type after: %u)\n", (unsigned)g_Settings->LoadDword(GameRunning_CPU_PausedType));
    printf("%s Settings closed with %s%s: paused inside %d, depth %d, paused after %d\n", (WasPausedInside && Depth == 1 && (UserPausedFirst ? Paused() : (Back && !Paused()))) ? "ok" : "BAD", How, UserPausedFirst ? " (the user had paused)" : "", (int)WasPausedInside, Depth, (int)Paused());
    CHECK(WasPausedInside);
    CHECK(Depth == 1);
    CHECK(ConfigPause::OpenCount() == 0);
    if (UserPausedFirst)
    {
        CHECK(Paused()); // not resumed by the end of the dialog
        ResumeIfPaused();
    }
    else
    {
        CHECK(Back && !Paused());
    }
}

static void PartPause()
{
    printf("part 2: the game stands still while a configuration window is open\n");
    g_Window->activateWindow();
    ResumeIfPaused();
    for (const char * How : {"cancel", "ok", "x", "apply-ok"})
    {
        OpenSettingsAndClose(How, false);
    }
    for (const char * How : {"cancel", "x"})
    {
        OpenSettingsAndClose(How, true);
    }
    // nested: two guards (the second is the dialog that Settings opens)
    {
        ConfigPause First;
        CHECK(WaitFor([]() { return Paused(); }, 5000));
        {
            ConfigPause Second;
            CHECK(ConfigPause::OpenCount() == 2);
        }
        Pump(500);
        CHECK(Paused()); // the first one is still open
        CHECK(ConfigPause::OpenCount() == 1);
    }
    const bool Resumed = WaitFor([]() { return !Paused(); }, 5000);
    printf("%s nested pauses: resumed only by the last one (%d)\n", Resumed ? "ok" : "BAD", (int)Resumed);
    CHECK(Resumed);
    CHECK(ConfigPause::OpenCount() == 0);
    // the real nesting: Settings -> the Input dialog of the plugin (ConfigPlugin) -> back to Settings
    {
        bool PausedInInput = false, PausedAfterInput = false, KeyBlocked = false;
        QTimer::singleShot(600, [&]() {
            QDialog * Settings = Find("SettingsDialog");
            if (Settings == nullptr)
            {
                Settings = Find(nullptr);
            }
            QTimer::singleShot(0, [&]() {
                g_Plugins->ConfigPlugin(reinterpret_cast<void *>(g_Window->winId()), PLUGIN_TYPE_CONTROLLER);
            });
            // (the input dialog is modal: it runs its own loop inside ConfigPlugin; this timer is the one that reaches it)
            QTimer::singleShot(1500, [&, Settings]() {
                QDialog * Input = nullptr;
                for (QWidget * W : QApplication::topLevelWidgets())
                {
                    QDialog * D = qobject_cast<QDialog *>(W);
                    if (D != nullptr && D->isVisible() && D->objectName() == "InputDialog")
                    {
                        Input = D;
                    }
                }
                CHECK(Input != nullptr);
                if (Input == nullptr)
                {
                    return;
                }
                Input->activateWindow();
                Pump(300);
                PausedInInput = Paused() && ConfigPause::OpenCount() == 2;
                // a key of the pad reaches neither the game nor a shortcut while the dialog is open
                Key(X_ESC, true);
                Pump(300);
                KeyBlocked = (Read() & 0xFFFF) == 0;
                Key(X_ESC, false);
                Pump(100);
                Input->findChild<QPushButton *>("VxCancel")->click();
                QTimer::singleShot(500, [&, Settings]() {
                    PausedAfterInput = Paused() && ConfigPause::OpenCount() == 1; // Settings is still open: the game must still stand
                    if (Settings != nullptr)
                    {
                        Settings->close();
                    }
                });
            });
        });
        SettingsDialog Dialog(false, g_Window);
        Dialog.exec();
        const bool Back = WaitFor([]() { return !Paused(); }, 5000);
        printf("%s Settings -> Input -> back: paused in Input %d, key blocked %d, still paused when Input closed %d, resumed at the end %d\n", (PausedInInput && KeyBlocked && PausedAfterInput && Back) ? "ok" : "BAD", (int)PausedInInput, (int)KeyBlocked, (int)PausedAfterInput, (int)Back);
        CHECK(PausedInInput);
        CHECK(KeyBlocked);
        CHECK(PausedAfterInput);
        CHECK(Back);
        CHECK(ConfigPause::OpenCount() == 0);
    }
    // a plugin dialog from the menu (Options > Configure plugin) is the same pause
    {
        bool PausedInside = false;
        QTimer::singleShot(1000, [&]() {
            QDialog * Input = Find("InputDialog");
            PausedInside = Paused();
            if (Input != nullptr)
            {
                Input->close(); // the X of the window
            }
        });
        g_Plugins->ConfigPlugin(reinterpret_cast<void *>(g_Window->winId()), PLUGIN_TYPE_CONTROLLER);
        const bool Back = WaitFor([]() { return !Paused(); }, 5000);
        printf("%s the Input dialog alone, closed with the X: paused inside %d, resumed %d\n", (PausedInside && Back) ? "ok" : "BAD", (int)PausedInside, (int)Back);
        CHECK(PausedInside);
        CHECK(Back);
        CHECK(ConfigPause::OpenCount() == 0);
    }
    // the shortcuts of the window work again after all that
    Settle();
    CHECK(Running() && !Paused());
}

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        printf("usage: %s <rom>\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setenv("SDL_VIDEODRIVER", "x11", 0);
    QApplication App(argc, argv);
    g_X = XOpenDisplay(nullptr);
    if (g_X == nullptr)
    {
        printf("no display\n");
        return 2;
    }
    MainWindow Window;
    g_Window = &Window;
    const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
    char * Args[] = {argv[0], nullptr};
    if (!AppInit(Window.GetNotify(), Base.c_str(), 1, Args))
    {
        return 2;
    }
    CDebuggerUI * Debugger = new CDebuggerUI;
    Window.CreateLanguageMenus();
    Window.InitPlugins();
    Window.show();
    Pump(500);
    g_Settings->SaveBool(Setting_ForceInterpreterCPU, true);
    Window.OpenImage(QString::fromLocal8Bit(argv[1]));
    CHECK(WaitFor(Running, 30000));
    Pump(2500);
    Settle();
    PartKeys();
    PartPause();
    Window.close();
    delete Debugger;
    AppCleanup();
    printf("%s hotkeys and pause\n", g_Failures == 0 ? "PASS" : "FAILED");
    return g_Failures == 0 ? 0 : 1;
}
