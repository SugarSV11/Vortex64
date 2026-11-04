// The keyboard and the mouse while a game runs in the main window of Vortex64: the focus is Qt's (the window of the
// application, not an SDL window of the test), the physical events are made by the X server (XTEST) with the pointer over
// the picture of the game, over the menu or the status bar, and outside the window, and the plugin has to give the state
// of the pad (GetKeys, as the emulator asks) whatever the place of the pointer is.
//
//   input-capture-test write     (the bindings of Player 1: the same directories)
//   input-ingame-test <rom>
#include "ui_test_env.h"
#include "../../Project64-input/LinuxScancodes.h"
#include "../MainWindow.h"
#include "../Debugger/DebuggerUI.h"

#include <Project64-core/Plugins/ControllerPlugin.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/N64System/SystemGlobals.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include "input_ui_access.h"
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <algorithm>
#include <set>
#include <vector>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTime>
#include <X11/Xlib.h>
#undef KeyPress
#undef KeyRelease
#undef None
#undef Bool
#include <SDL.h>
#include <unistd.h>

extern "C" int XTestFakeKeyEvent(Display *, unsigned int, int, unsigned long);
extern "C" int XTestFakeButtonEvent(Display *, unsigned int, int, unsigned long);
extern "C" int XTestFakeMotionEvent(Display *, int, int, int, unsigned long);

static Display * g_X;

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

static void Pump(int Ms)
{
    for (int i = 0; i < Ms / 10; i++)
    {
        QApplication::processEvents();
        usleep(10000);
    }
}

enum Kind { KEY, BUTTON };
struct Case
{
    const char * Name;
    Kind Type;
    unsigned Code;  // X keycode / X button
    uint32_t Mask;  // the bits that must come on
};
// bindings of `input-capture-test write` (the rows of Player 1)
static const Case Cases[] = {
    {"Left Shift -> Down", KEY, 50, 1u << 2},
    {"Right Ctrl -> Left", KEY, 105, 1u << 1},
    {"Escape -> Right", KEY, 9, 1u << 0},
    {"Keypad 1 -> C Up", KEY, 87, 1u << 11},
    {"F8 -> C Down", KEY, 74, 1u << 10},
    {"Mouse left -> C Left", BUTTON, 1, 1u << 9},
    {"Mouse middle -> C Right", BUTTON, 2, 1u << 8},
    {"Mouse right -> B", BUTTON, 3, 1u << 6},
    {"Mouse Back -> A", BUTTON, 8, 1u << 7},
    {"Super -> R", KEY, 133, 1u << 12},
    {"Wheel up -> Start", BUTTON, 4, 1u << 4},
    {"Wheel down -> Z", BUTTON, 5, 1u << 5},
    {"Wheel left -> L", BUTTON, 6, 1u << 13},
};


// ---------------------------------------------------------------------------------------------------------------------
// The matrix: every key that the system identifies, every button of the mouse and the wheel. Each is assigned with the real
// dialog (the X server makes the events, as for a user: the click on the button of a row, then the input), the dialog is
// closed with OK, and the game that runs in the window is given the same input: the pad must answer as the row says.
// Rows of Player 1: Pad U D L R, Analog U D L R, C U D L R, B, A, Start, Z, L, R
struct Row
{
    uint32_t Mask; // digital bits
    int Axis;      // 1: X axis, 2: Y axis, 0: none
};
static const Row Rows[18] = {{1u << 3, 0}, {1u << 2, 0}, {1u << 1, 0}, {1u << 0, 0}, {0, 2}, {0, 2}, {0, 1}, {0, 1}, {1u << 11, 0}, {1u << 10, 0}, {1u << 9, 0}, {1u << 8, 0}, {1u << 6, 0}, {1u << 7, 0}, {1u << 4, 0}, {1u << 5, 0}, {1u << 13, 0}, {1u << 12, 0}};

struct Input
{
    std::string Name;
    Kind Type;
    unsigned Code; // X keycode / X button
};
static std::vector<Input> g_Inputs;
static size_t g_BatchStart = 0, g_BatchSize = 0;
static int g_MatrixFailures = 0;
static std::vector<std::string> g_NotAccepted, g_Ended;

static void MoveTo(QWidget * W, QPoint Local)
{
    const QPoint G = W->mapToGlobal(Local);
    XTestFakeMotionEvent(g_X, -1, G.x(), G.y(), 0);
    XFlush(g_X);
    Pump(40);
}

static void Click(unsigned Button)
{
    XTestFakeButtonEvent(g_X, Button, 1, 0);
    XFlush(g_X);
    Pump(50);
    XTestFakeButtonEvent(g_X, Button, 0, 0);
    XFlush(g_X);
    Pump(50);
}

static QDialog * FindInputDialog()
{
    for (QWidget * W : QApplication::topLevelWidgets())
    {
        QDialog * D = qobject_cast<QDialog *>(W);
        if (D != nullptr && D->isVisible() && D->objectName() == "InputDialog")
        {
            return D;
        }
    }
    return nullptr;
}

static std::vector<bool> g_Accepted;

// the dialog is open (ConfigPlugin runs its loop): the batch is assigned to the rows and OK is pressed
static void DriveDialog()
{
    QDialog * Dialog = FindInputDialog();
    if (Dialog == nullptr)
    {
        QTimer::singleShot(100, DriveDialog);
        return;
    }
    Dialog->activateWindow();
    Dialog->raise();
    Pump(300);
    QWidget * Page = Dialog->findChildren<QStackedWidget *>().first()->widget(0);
    QList<BindingRef> Displays = BindingDisplays(Page);
    QList<QPushButton *> Scans;
    for (QPushButton * B : Page->findChildren<QPushButton *>())
    {
        if (B->objectName() == "VxScan")
        {
            Scans.push_back(B);
        }
    }
    std::sort(Scans.begin(), Scans.end(), [](QPushButton * A, QPushButton * B) { return A->property("row").toInt() < B->property("row").toInt(); });
    CHECK(Displays.size() == 18 && Scans.size() == 18);
    QComboBox * Type = Page->findChildren<QComboBox *>().at(0);
    Type->setCurrentIndex(Type->findData(1));
    Pump(100);
    QWidget * Neutral = Page->findChildren<QLabel *>().isEmpty() ? Page : static_cast<QWidget *>(Page->findChildren<QLabel *>().at(0));
    g_Accepted.assign(g_BatchSize, false);
    for (size_t i = 0; i < g_BatchSize && Displays.size() == 18; i++)
    {
        const Input & In = g_Inputs[g_BatchStart + i];
        for (QWidget * Parent = Scans.at((int)i)->parentWidget(); Parent != nullptr; Parent = Parent->parentWidget())
        {
            if (QScrollArea * Area = qobject_cast<QScrollArea *>(Parent))
            {
                Area->ensureWidgetVisible(Scans.at((int)i)); // (the rows below the fold are reached as a user does: scrolling)
            }
        }
        Pump(60);
        MoveTo(Scans.at((int)i), Scans.at((int)i)->rect().center());
        Click(1);
        const bool Waiting = Displays.at((int)i)->text() == "...";
        MoveTo(Neutral, Neutral->rect().center());
        if (In.Type == KEY)
        {
            XTestFakeKeyEvent(g_X, In.Code, 1, 0);
            XFlush(g_X);
            Pump(50);
            XTestFakeKeyEvent(g_X, In.Code, 0, 0);
            XFlush(g_X);
            Pump(50);
        }
        else
        {
            Click(In.Code);
        }
        Pump(100);
        const QString Shown = Displays.at((int)i)->text();
        g_Accepted[i] = Waiting && !Shown.isEmpty() && Shown != "..." && FindInputDialog() == Dialog;
        if (!g_Accepted[i])
        {
            printf("BAD dialog %s: waiting=%d shows '%s'\n", In.Name.c_str(), Waiting, Shown.toUtf8().constData());
            g_MatrixFailures++;
            if (Displays.at((int)i)->text() == "...") // leave the capture
            {
                XTestFakeKeyEvent(g_X, 9, 1, 0);
                XTestFakeKeyEvent(g_X, 9, 0, 0);
                XFlush(g_X);
                Pump(100);
            }
            if (FindInputDialog() != Dialog)
            {
                return;
            }
        }
    }
    Dialog->findChild<QPushButton *>("VxOk")->click();
}

static QString g_RomPath;

// Mode: "matrix" (all the batches in this process), "matrix-write:K" (batch K assigned with the dialog and OK, nothing else),
// "matrix-read:K" (batch K of a process that did not assign it: what the file of settings has)
static void RunMatrix(MainWindow & Window, const std::string & Mode)
{
    int OnlyBatch = -1;
    const bool WriteOnly = Mode.rfind("matrix-write:", 0) == 0, ReadOnly = Mode.rfind("matrix-read:", 0) == 0;
    if (WriteOnly || ReadOnly)
    {
        OnlyBatch = atoi(Mode.substr(Mode.find(':') + 1).c_str());
    }
    // no shortcut of the window takes a key from the game in this part (they are the subject of the other one)
    for (QAction * Action : Window.findChildren<QAction *>())
    {
        Action->setShortcuts({});
    }
    // the keys: every X keycode whose evdev code SDL identifies; the mouse: the 3 buttons, the others, the wheel
    // (two codes of the keyboard that SDL names the same key are the same input for the game: the first is tried and the
    // others are reported)
    std::set<int> Identities;
    for (unsigned Evdev = 1; Evdev < 248; Evdev++)
    {
        if (linux_scancode_table[Evdev] == SDL_SCANCODE_UNKNOWN)
        {
            continue;
        }
        if (!Identities.insert((int)linux_scancode_table[Evdev]).second)
        {
            printf("NOTE evdev %u is the same key as an earlier one for SDL (%s)\n", Evdev, SDL_GetScancodeName(linux_scancode_table[Evdev]));
            continue;
        }
        const char * Name = SDL_GetScancodeName(linux_scancode_table[Evdev]);
        g_Inputs.push_back({std::string("key ") + std::to_string(Evdev) + " (" + (Name != nullptr && Name[0] != 0 ? Name : "?") + ")", KEY, Evdev + 8});
    }
    // the buttons the pointer of the X server has (the virtual pointer of XTEST of this server)
    unsigned char Map[256];
    const int ServerButtons = XGetPointerMapping(g_X, Map, 256);
    printf("matrix: the pointer of the X server has %d buttons\n", ServerButtons);
    for (unsigned Button : {1u, 2u, 3u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u})
    {
        if ((int)Button <= ServerButtons)
        {
            g_Inputs.push_back({"mouse button " + std::to_string(Button), BUTTON, Button});
        }
    }
    for (unsigned Wheel : {4u, 5u, 6u, 7u})
    {
        g_Inputs.push_back({"wheel " + std::to_string(Wheel), BUTTON, Wheel});
    }
    printf("matrix: %d inputs\n", (int)g_Inputs.size());
    int Reached = 0, Tested = 0;
    printf("matrix: %d batches\n", (int)((g_Inputs.size() + 17) / 18));
    for (size_t Start = 0; Start < g_Inputs.size(); Start += 18)
    {
        if (OnlyBatch >= 0 && (int)(Start / 18) != OnlyBatch)
        {
            continue;
        }
        g_BatchStart = Start;
        g_BatchSize = std::min<size_t>(18, g_Inputs.size() - Start);
        if (ReadOnly)
        {
            g_Accepted.assign(g_BatchSize, true); // (assigned by the process that wrote it)
        }
        else
        {
            QTimer::singleShot(300, DriveDialog);
            g_Plugins->ConfigPlugin(reinterpret_cast<void *>(Window.winId()), PLUGIN_TYPE_CONTROLLER);
            Window.activateWindow();
            Pump(400);
            if (WriteOnly)
            {
                int Accepted = 0;
                for (bool A : g_Accepted)
                {
                    Accepted += A ? 1 : 0;
                }
                printf("matrix write %d: %d of %d inputs assigned with the dialog\n", OnlyBatch, Accepted, (int)g_BatchSize);
                g_Failures += g_MatrixFailures;
                return;
            }
        }
        // the same inputs, now to the game
        for (size_t i = 0; i < g_BatchSize; i++)
        {
            const Input & In = g_Inputs[g_BatchStart + i];
            if (!g_Accepted[i])
            {
                continue;
            }
            const Row & R = Rows[i];
            // (a tap of Alt makes the menu bar of Qt take the keys, and Up opens a menu: that menu has the keys, as it should; the
            // user closes it with Esc, here it is closed)
            while (QWidget * Popup = QApplication::activePopupWidget())
            {
                Popup->close();
                Pump(50);
            }
            Window.menuBar()->setActiveAction(nullptr);
            Window.activateWindow();
            if (QApplication::focusWidget() != nullptr && qobject_cast<QMenuBar *>(QApplication::focusWidget()) != nullptr)
            {
                Window.centralWidget()->setFocus();
            }
            if (In.Type == BUTTON)
            {
                // (a button is pressed over the picture of the game)
                QWidget * Video = Window.centralWidget();
                const QPoint G = Video->mapToGlobal(Video->rect().center());
                XTestFakeMotionEvent(g_X, -1, G.x(), G.y(), 0);
                XFlush(g_X);
                Pump(80);
            }
            if (In.Type == KEY)
            {
                XTestFakeKeyEvent(g_X, In.Code, 1, 0);
            }
            else
            {
                XTestFakeButtonEvent(g_X, In.Code, 1, 0);
                if (In.Code >= 4 && In.Code <= 7) // (a wheel click is a press and a release at once)
                {
                    XTestFakeButtonEvent(g_X, In.Code, 0, 0);
                }
            }
            XFlush(g_X);
            uint32_t State = 0;
            bool Ok = false;
            for (int t = 0; t < 40 && !Ok; t++)
            {
                Pump(20);
                State = Read();
                const BUTTONS B = {State};
                Ok = R.Axis == 0 ? ((State & 0xFFFF) == R.Mask && B.X_AXIS == 0 && B.Y_AXIS == 0) : (R.Axis == 1 ? ((State & 0xFFFF) == 0 && B.X_AXIS != 0 && B.Y_AXIS == 0) : ((State & 0xFFFF) == 0 && B.Y_AXIS != 0 && B.X_AXIS == 0));
            }
            if (In.Type == KEY)
            {
                XTestFakeKeyEvent(g_X, In.Code, 0, 0);
            }
            else if (!(In.Code >= 4 && In.Code <= 7))
            {
                XTestFakeButtonEvent(g_X, In.Code, 0, 0);
            }
            XFlush(g_X);
            Tested++;
            Pump(120);
            if (Running() && (Read() & 0xFFFF) != 0)
            {
                printf("BAD game %s: still down after the release, state %08x\n", In.Name.c_str(), Read());
                g_MatrixFailures++;
            }
            if (!Running())
            {
                // something of the window took the key and ended the game (the shortcuts of the menu): it starts again
                printf("NOTE %s ended the game (the window has a function for it)\n", In.Name.c_str());
                g_Ended.push_back(In.Name);
                Window.OpenImage(g_RomPath);
                WaitFor(Running, 30000);
                Pump(1500);
                Window.activateWindow();
            }
            if (Ok)
            {
                Reached++;
            }
            else
            {
                printf("BAD game %s -> row %d: state %08x (game running %d, paused %d, active window %s, title '%s')\n", In.Name.c_str(), (int)i, State, (int)Running(), (int)g_Settings->LoadBool(GameRunning_CPU_Paused), QApplication::activeWindow() == &Window ? "ours" : "other", Window.windowTitle().toUtf8().constData());
                g_MatrixFailures++;
            }
            if (Window.isFullScreen())
            {
                Window.showNormal();
            }
            Pump(100);
        }
    }
    printf("matrix: %d of %d inputs reach the pad (%s)\n", Reached, Tested, ReadOnly ? "assigned by an earlier process: the file of settings" : "every one assigned with the dialog");
    const int Expected = OnlyBatch >= 0 ? (int)g_BatchSize : (int)g_Inputs.size();
    if (Tested != Expected)
    {
        printf("matrix: %d inputs were not accepted by the dialog\n", Expected - Tested);
    }
    g_Failures += g_MatrixFailures + (Tested != Expected ? 1 : 0);
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
    CHECK(g_Plugins->Control() != nullptr && g_Plugins->Control()->GetKeys != nullptr);
    Window.OpenImage(QString::fromLocal8Bit(argv[1]));
    CHECK(WaitFor(Running, 30000));
    Pump(2500);
    Window.activateWindow();
    Window.raise();
    Pump(500);

    g_RomPath = QString::fromLocal8Bit(argv[1]);
    if (argc > 2 && std::string(argv[2]).rfind("matrix", 0) == 0)
    {
        RunMatrix(Window, argv[2]);
        Window.close();
        delete Debugger;
        AppCleanup(); // (as main(): the plugins end with their threads before the process unloads them)
        printf("%s input matrix\n", g_Failures == 0 ? "PASS" : "FAILED");
        return g_Failures == 0 ? 0 : 1;
    }
    QWidget * Video = Window.centralWidget();
    struct Place
    {
        const char * Name;
        QPoint Global;
    };
    const QPoint VideoCentre = Video->mapToGlobal(Video->rect().center());
    const QPoint MenuBar = Window.menuBar()->isVisible() ? Window.menuBar()->mapToGlobal(QPoint(Window.menuBar()->width() - 20, 5)) : Window.mapToGlobal(QPoint(Window.width() - 20, 5));
    const QPoint StatusBar = Window.statusBar()->isVisible() ? Window.statusBar()->mapToGlobal(QPoint(Window.statusBar()->width() / 2, 5)) : MenuBar;
    const Place Places[] = {{"over the picture", VideoCentre}, {"over the menu bar", MenuBar}, {"over the status bar", StatusBar}, {"outside the window", QPoint(3, 3)}};
    printf("SDL: video initialised %d, driver %s, keyboard focus window %p, mouse focus window %p\n", (int)(SDL_WasInit(SDL_INIT_VIDEO) != 0), SDL_GetCurrentVideoDriver() != nullptr ? SDL_GetCurrentVideoDriver() : "none", (void *)SDL_GetKeyboardFocus(), (void *)SDL_GetMouseFocus());
    printf("window %dx%d at %d,%d, active window: %s\n", Window.width(), Window.height(), Window.x(), Window.y(), QApplication::activeWindow() == &Window ? "this one" : "another");

    int Done = 0, Total = 0;
    for (const Place & P : Places)
    {
        const bool Outside = P.Global == QPoint(3, 3);
        XTestFakeMotionEvent(g_X, -1, P.Global.x(), P.Global.y(), 0);
        XFlush(g_X);
        Pump(150);
        for (const Case & C : Cases)
        {
            if (Outside && C.Type == BUTTON)
            {
                continue; // (a click outside the window is not an event of the application)
            }
            Total++;
            if (getenv("VORTEX_INPUT_TRACE") != nullptr)
            {
                fprintf(stderr, "[test] %s press at %s\n", C.Name, QTime::currentTime().toString("ss.zzz").toUtf8().constData());
            }
            if (C.Type == KEY)
            {
                XTestFakeKeyEvent(g_X, C.Code, 1, 0);
            }
            else
            {
                XTestFakeButtonEvent(g_X, C.Code, 1, 0);
            }
            XFlush(g_X);
            if (C.Type == BUTTON && C.Code >= 4 && C.Code <= 7) // (a wheel click is a press and a release at once)
            {
                XTestFakeButtonEvent(g_X, C.Code, 0, 0);
                XFlush(g_X);
            }
            uint32_t State = 0;
            for (int i = 0; i < 40 && (State & C.Mask) != C.Mask; i++)
            {
                Pump(20);
                State = Read();
            }
            if (C.Type == KEY)
            {
                XTestFakeKeyEvent(g_X, C.Code, 0, 0);
            }
            else
            {
                XTestFakeButtonEvent(g_X, C.Code, 0, 0);
            }
            XFlush(g_X);
            if (Window.isFullScreen()) // (Esc is also the default shortcut of the full screen)
            {
                Window.showNormal();
                Pump(300);
            }
            const bool Ok = (State & 0xFFFF) == C.Mask;
            Pump(150);
            const uint32_t Idle = Read();
            if (Idle != 0) // (what was pressed is released: nothing stays down)
            {
                printf("BAD %s, pointer %s: still down after the release, state %08x\n", C.Name, P.Name, Idle);
                g_Failures++;
            }
            printf("%s %s, pointer %s: state %08x\n", Ok ? "ok" : "BAD", C.Name, P.Name, State);
            if (Ok)
            {
                Done++;
            }
            else
            {
                g_Failures++;
            }
            Pump(120);
        }
    }
    // an open menu has the keys and the clicks: they do not reach the game; once it is closed they do
    {
        QMenu * Menu = nullptr;
        for (QAction * Action : Window.menuBar()->actions())
        {
            if (Menu == nullptr && Action->menu() != nullptr && !Action->menu()->actions().isEmpty())
            {
                Menu = Action->menu();
            }
        }
        CHECK(Menu != nullptr);
        if (Menu != nullptr)
        {
            Menu->popup(Window.mapToGlobal(QPoint(40, 40)));
            Pump(300);
            CHECK(QApplication::activePopupWidget() != nullptr);
            XTestFakeMotionEvent(g_X, -1, VideoCentre.x(), VideoCentre.y(), 0);
            XTestFakeKeyEvent(g_X, 50, 1, 0); // Left Shift -> Down
            XFlush(g_X);
            uint32_t State = 0;
            for (int i = 0; i < 15; i++)
            {
                Pump(20);
                State |= Read();
            }
            XTestFakeKeyEvent(g_X, 50, 0, 0);
            XFlush(g_X);
            printf("%s a key with a menu open: state %08x (the game must not see it)\n", (State & 0xFFFF) == 0 ? "ok" : "BAD", State);
            CHECK((State & 0xFFFF) == 0);
            Menu->hide();
            Pump(300);
            CHECK(QApplication::activePopupWidget() == nullptr);
            Window.activateWindow();
            Pump(200);
            XTestFakeKeyEvent(g_X, 50, 1, 0);
            XFlush(g_X);
            State = 0;
            for (int i = 0; i < 40 && (State & 4) == 0; i++)
            {
                Pump(20);
                State = Read();
            }
            XTestFakeKeyEvent(g_X, 50, 0, 0);
            XFlush(g_X);
            printf("%s the same key with the menu closed: state %08x\n", (State & 0xFFFF) == 4 ? "ok" : "BAD", State);
            CHECK((State & 0xFFFF) == 4);
        }
    }
    Window.close();
    delete Debugger;
        AppCleanup(); // (as main(): the plugins end with their threads before the process unloads them)
    printf("%s input in game: %d of %d inputs reach the N64 pad\n", g_Failures == 0 ? "PASS" : "FAILED", Done, Total);
    return g_Failures == 0 ? 0 : 1;
}
