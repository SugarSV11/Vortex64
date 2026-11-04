// Main window test: what CMainGui does besides the menu (Project64/UserInterface/MainWindow.cpp)
// with the real core and a real ROM. Needs an X server (Scripts/linux/run-gui-tests.sh starts Xvfb
// and isolates the XDG directories):
//
//   mainwindow-test <rom>
//
// The two panes of the status bar and the time rule of the messages, the window place and size of
// the browser and of a game (remembered, restored), the caption and the recent ROMs from the game
// that is loaded, the browser coming back when the game ends, pausing when the window or the
// application loses the focus (Setting_AutoSleep), the screen saver request, leaving the full
// screen for dialogs, dropped files, the status bar setting and the browser setting.
#include <cstdlib>
#include "../Debugger/DebuggerUI.h"
#include "../MainMenu.h"
#include "../MainWindow.h"
#include "../Vortex/Store.h"
#include "../Debugger/ScriptShim/Project64/UserInterface/MainWindow.h" // JSAPI_ACT_*
#include "../ScreenSaverInhibit.h"

#include <Common/path.h>
#include <Project64-core/AppInit.h>
#include <Project64-core/Notification.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Settings.h>
#include <Project64/Settings/UISettings.h>

#include <QApplication>
#include <QTemporaryDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QStatusBar>
#include <QToolButton>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <arpa/inet.h>
#include <cstdio>
#include <mutex>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>
#include <atomic>
#include <functional>
#include <thread>

static int g_Failures = 0;
#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if (!(cond))                                               \
        {                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_Failures++;                                          \
        }                                                          \
    } while (0)

static void DismissDialogs()
{
    for (QWidget * Widget : QApplication::topLevelWidgets())
    {
        QMessageBox * Box = qobject_cast<QMessageBox *>(Widget);
        if (Box != nullptr && Box->isVisible())
        {
            printf("dialog: %s\n", Box->text().left(60).toUtf8().constData());
            Box->accept();
        }
    }
}

static bool WaitFor(const std::function<bool()> & Condition, int TimeoutMs)
{
    QElapsedTimer Timer;
    Timer.start();
    while (Timer.elapsed() < TimeoutMs)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (Condition())
        {
            return true;
        }
        QThread::msleep(10);
    }
    return Condition();
}

static void Pump(int Ms)
{
    WaitFor([]() { return false; }, Ms);
}

static bool Running()
{
    return g_Settings->LoadBool(GameRunning_CPU_Running);
}

static bool Paused()
{
    return g_Settings->LoadBool(GameRunning_CPU_Paused);
}

// The Discord client, as far as the rich presence needs it: the Unix socket "discord-ipc-0" in the
// runtime directory, the handshake and the frames of SET_ACTIVITY. Frames are [opcode][length]
// (32 bits each, little endian) and a JSON text.
struct FakeDiscord
{
    int Listener = -1;
    std::thread Thread;
    std::atomic<bool> Stop{false};
    std::mutex Lock;
    std::vector<std::pair<uint32_t, std::string>> Frames;
    std::atomic<int> Connections{0}, Disconnections{0};

    bool Start(const std::string & Directory)
    {
        Listener = socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un Address = {};
        Address.sun_family = AF_UNIX;
        snprintf(Address.sun_path, sizeof(Address.sun_path), "%s/discord-ipc-0", Directory.c_str());
        if (bind(Listener, (sockaddr *)&Address, sizeof(Address)) != 0 || listen(Listener, 1) != 0)
        {
            return false;
        }
        Thread = std::thread([this]() { Serve(); });
        return true;
    }
    static bool ReadAll(int Fd, void * Buffer, size_t Length, std::atomic<bool> & Stop)
    {
        char * Data = (char *)Buffer;
        while (Length > 0 && !Stop)
        {
            pollfd Poll = {Fd, POLLIN, 0};
            if (poll(&Poll, 1, 100) <= 0)
            {
                continue;
            }
            const ssize_t Count = read(Fd, Data, Length);
            if (Count <= 0)
            {
                return false;
            }
            Data += Count;
            Length -= Count;
        }
        return Length == 0;
    }
    static void WriteFrame(int Fd, uint32_t Opcode, const std::string & Json)
    {
        uint32_t Header[2] = {Opcode, (uint32_t)Json.size()};
        write(Fd, Header, sizeof(Header));
        write(Fd, Json.data(), Json.size());
    }
    void Serve()
    {
        while (!Stop)
        {
            pollfd Poll = {Listener, POLLIN, 0};
            if (poll(&Poll, 1, 100) <= 0)
            {
                continue;
            }
            const int Client = accept(Listener, nullptr, nullptr);
            if (Client < 0)
            {
                continue;
            }
            Connections++;
            for (;;)
            {
                uint32_t Header[2];
                if (!ReadAll(Client, Header, sizeof(Header), Stop))
                {
                    break;
                }
                std::string Json(Header[1], '\0');
                if (!ReadAll(Client, &Json[0], Header[1], Stop))
                {
                    break;
                }
                {
                    std::lock_guard<std::mutex> Guard(Lock);
                    Frames.push_back({Header[0], Json});
                }
                if (Header[0] == 0) // handshake: the client is ready
                {
                    WriteFrame(Client, 1, "{\"cmd\":\"DISPATCH\",\"evt\":\"READY\",\"data\":{\"v\":1,\"user\":{\"id\":\"1\",\"username\":\"tester\",\"discriminator\":\"0\",\"avatar\":\"\"}}}");
                }
            }
            close(Client);
            Disconnections++;
        }
    }
    // The frames (as text) so far
    std::string All()
    {
        std::lock_guard<std::mutex> Guard(Lock);
        std::string Text;
        for (const auto & Frame : Frames)
        {
            Text += std::to_string(Frame.first) + ":" + Frame.second + "\n";
        }
        return Text;
    }
    ~FakeDiscord()
    {
        Stop = true;
        if (Thread.joinable())
        {
            Thread.join();
        }
        if (Listener >= 0)
        {
            close(Listener);
        }
    }
};

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        printf("usage: %s <rom>\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const QString Rom = QString::fromLocal8Bit(argv[1]);
    setenv("SDL_VIDEODRIVER", "x11", 0);
    QApplication App(argc, argv);
    // the Discord client of the rich presence (Setting_EnableDiscordRPC is on by default, and the
    // main window starts it): in a runtime directory of its own
    QTemporaryDir Runtime;
    CHECK(Runtime.isValid());
    FakeDiscord Discord;
    setenv("XDG_RUNTIME_DIR", Runtime.path().toUtf8().constData(), 1);
    CHECK(Discord.Start(Runtime.path().toStdString()));
    MainWindow Window;
    QTimer DismissTimer;
    QObject::connect(&DismissTimer, &QTimer::timeout, &DismissDialogs);
    DismissTimer.start(50);
    const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
    char * Args[] = {argv[0], nullptr};
    if (!AppInit(Window.GetNotify(), Base.c_str(), 1, Args))
    {
        return 2;
    }
    // as main(): the debugger object always exists (the settings may have the debugger on)
    CDebuggerUI * Debugger = new CDebuggerUI;
    Window.CreateLanguageMenus();
    Window.InitPlugins();
    Window.show();
    Pump(500);
    // PJ64_TEST_JIT=1: the test runs on the recompiler instead of the interpreter
    const bool TestJit = getenv("PJ64_TEST_JIT") != nullptr;
    g_Settings->SaveBool(Setting_ForceInterpreterCPU, !TestJit);
    if (TestJit)
    {
        g_Settings->SaveDword(Game_CpuType, CPU_Recompiler);
    }
    UISettingsSaveBool(Setting_DisableScrSaver, true);
    UISettingsSaveBool(Setting_AutoSleep, true);
    QStackedWidget * Stack = qobject_cast<QStackedWidget *>(Window.centralWidget());
    CHECK(Stack != nullptr);

    // ---- the browser ----
    CHECK(Window.RomBrowserShown());
    CHECK(Stack->width() >= 900 && Stack->height() >= 580); // (the library window has a minimum size)
    printf("mainwindow: browser %dx%d\n", Stack->width(), Stack->height());

    // ---- the status bar: two panes, the time rule of the messages ----
    g_Notify->DisplayMessage(5, "timed message");
    CHECK(WaitFor([&]() { return Window.StatusPane(0)->text() == "timed message"; }, 2000));
    g_Notify->DisplayMessage(0, "low priority");
    Pump(200);
    CHECK(Window.StatusPane(0)->text() == "timed message"); // held for 5 seconds against messages without a time
    g_Notify->DisplayMessage2("second pane");
    CHECK(WaitFor([&]() { return Window.StatusPane(1)->text() == "second pane"; }, 2000));
    CHECK(Window.StatusPane(0)->text() == "timed message");
    printf("mainwindow: status bar\n");

    // the library has no status bar and no menu bar (they belong to a running game)
    CHECK(!Window.statusBar()->isVisible());
    CHECK(!Window.menuBar()->isVisible());

    // ---- FatalError: the message is shown and the calling thread ends ----
    {
        std::atomic<bool> After{false}, Ended{false};
        struct Guard
        {
            std::atomic<bool> & Flag;
            ~Guard()
            {
                Flag = true; // also when the thread is unwound by pthread_exit
            }
        };
        std::thread Fatal([&]() {
            Guard Sentinel{Ended};
            g_Notify->FatalError("fatal error of a thread");
            After = true;
        });
        CHECK(WaitFor([&]() { return Ended.load(); }, 5000));
        Fatal.join();
        CHECK(!After);
        printf("mainwindow: fatal error ended its thread\n");
    }

    // ---- place and size of the browser are remembered ----
    Window.move(123, 77);
    Pump(1400);
    CHECK(UISettingsLoadDword(RomBrowser_Left) == 123 && UISettingsLoadDword(RomBrowser_Top) == 77);
    Window.resize(720, 560);
    Pump(400);
    const QSize BrowserSize = Stack->size();
    CHECK(Vortex::Store::Instance().Value("window.w").toInt() == BrowserSize.width() && Vortex::Store::Instance().Value("window.h").toInt() == BrowserSize.height());
    CHECK(BrowserSize.width() >= 900);
    printf("mainwindow: browser place and size %dx%d\n", BrowserSize.width(), BrowserSize.height());

    // ---- a game: fixed video area, caption, recent ROMs ----
    Window.OpenImage(Rom);
    CHECK(WaitFor(Running, 30000));
    Pump(1000);
    CHECK(!Window.RomBrowserShown());
    CHECK(Stack->size() == QSize(640, 480));
    CHECK(Window.windowTitle().startsWith(QString::fromUtf8("Vortex64 \u2014 ")));
    CHECK(Window.windowTitle() != "Vortex64");
    CHECK(UISettingsLoadStringIndex(File_RecentGameFileIndex, 0) == Rom.toStdString());
    printf("mainwindow: game '%s'\n", Window.windowTitle().toUtf8().constData());
    Window.move(300, 200);
    Pump(1400);
    CHECK(UISettingsLoadDword(UserInterface_MainWindowLeft) == 300 && UISettingsLoadDword(UserInterface_MainWindowTop) == 200);
    // the browser's place is not the game's
    CHECK(UISettingsLoadDword(RomBrowser_Left) == 123);
    // the status bar of a game follows its setting; the menu bar is there
    CHECK(Window.menuBar()->isVisible() && Window.statusBar()->isVisible());
    CHECK(Window.StateLabel()->text().contains("Emulando") || Window.StateLabel()->text().contains("Running"));
    UISettingsSaveBool(UserInterface_ShowStatusBar, false);
    CHECK(WaitFor([&]() { return !Window.statusBar()->isVisible(); }, 2000));
    UISettingsSaveBool(UserInterface_ShowStatusBar, true);
    CHECK(WaitFor([&]() { return Window.statusBar()->isVisible(); }, 2000));

    // the status bar of a game window of 640 px with the longest texts: nothing is cut (every pane that shows is whole inside the bar), the state, the
    // speed and the two buttons are always there, and what gives way is the message (cut with "...") and the optional panes, never the speed
    {
        Window.ShowStatusMessage2("VI/s: 59.9 DL/s: 30.0");
        Window.ShowStatusMessage("A rather long message of the emulator that does not fit in the bar next to everything else", 0);
        Pump(1500);
        QStatusBar * Bar = Window.statusBar();
        CHECK(Bar->width() <= 700);
        int Shown = 0;
        for (QLabel * Label : Bar->findChildren<QLabel *>())
        {
            if (!Label->isVisibleTo(Bar) || Label->text().isEmpty())
            {
                continue;
            }
            Shown++;
            const QRect Place = Label->geometry();
            const bool Whole = Place.left() >= 0 && Place.right() <= Bar->width() && Place.width() >= Label->sizeHint().width() - 1;
            if (!Whole)
            {
                printf("mainwindow: status pane '%s' is cut (%d..%d of %d, text %d)\n", Label->objectName().toUtf8().constData(), Place.left(), Place.right(), Bar->width(),
                       Label->fontMetrics().horizontalAdvance(Label->text()));
            }
            CHECK(Whole);
        }
        CHECK(Shown >= 3);
        CHECK(Window.StateLabel()->isVisibleTo(Bar));
        QLabel * Speed = Bar->findChild<QLabel *>("VxStatusSpeed");
        CHECK(Speed != nullptr && Speed->isVisibleTo(Bar) && Speed->text().contains("VI/s"));
        QToolButton * Pause = Bar->findChild<QToolButton *>("VxStatusPause");
        QToolButton * Full = Bar->findChild<QToolButton *>("VxStatusFullscreen");
        CHECK(Pause != nullptr && Pause->isVisibleTo(Bar) && Pause->geometry().right() <= Bar->width());
        CHECK(Full != nullptr && Full->isVisibleTo(Bar) && Full->geometry().right() <= Bar->width());
        printf("mainwindow: status bar of %d px, %d panes, none cut\n", Bar->width(), Shown);
        Window.ShowStatusMessage2("");
        Window.ShowStatusMessage("", 0);
    }

    // ---- the screen saver is refused while the game runs and is not paused ----
    CHECK(Window.ScreenSaver()->Wanted());
    g_BaseSystem->ExternalEvent(SysEvent_PauseCPU_FromMenu);
    CHECK(WaitFor(Paused, 5000));
    CHECK(WaitFor([&]() { return !Window.ScreenSaver()->Wanted(); }, 2000));
    g_BaseSystem->ExternalEvent(SysEvent_ResumeCPU_FromMenu);
    CHECK(WaitFor([]() { return !Paused(); }, 5000));
    CHECK(WaitFor([&]() { return Window.ScreenSaver()->Wanted(); }, 2000));
    UISettingsSaveBool(Setting_DisableScrSaver, false);
    CHECK(!Window.ScreenSaver()->Wanted());
    UISettingsSaveBool(Setting_DisableScrSaver, true);
    CHECK(Window.ScreenSaver()->Wanted());
    if (Window.ScreenSaver()->Service().isEmpty())
    {
        printf("mainwindow: no desktop screen saver service on the session bus: the request is not sent\n");
    }
    else
    {
        CHECK(WaitFor([&]() { return Window.ScreenSaver()->Inhibited(); }, 5000));
        printf("mainwindow: screen saver inhibited through %s\n", Window.ScreenSaver()->Service().toUtf8().constData());
    }

    // ---- Setting_AutoSleep: the game pauses when the window or the application loses the focus ----
    Window.WindowFocusChanged(false);
    CHECK(WaitFor(Paused, 5000));
    CHECK(g_Settings->LoadDword(GameRunning_CPU_PausedType) == PauseType_AppLostFocus);
    Window.ApplicationActiveChanged(true); // another kind of pause is not ended by this
    Pump(300);
    CHECK(Paused());
    Window.WindowFocusChanged(true);
    CHECK(WaitFor([]() { return !Paused(); }, 5000));
    Window.ApplicationActiveChanged(false);
    CHECK(WaitFor(Paused, 5000));
    CHECK(g_Settings->LoadDword(GameRunning_CPU_PausedType) == PauseType_AppLostActive);
    Window.ApplicationActiveChanged(true);
    CHECK(WaitFor([]() { return !Paused(); }, 5000));
    // with the setting off nothing pauses
    UISettingsSaveBool(Setting_AutoSleep, false);
    Window.WindowFocusChanged(false);
    Window.ApplicationActiveChanged(false);
    Pump(500);
    CHECK(!Paused());
    Window.WindowFocusChanged(true);
    Window.ApplicationActiveChanged(true);
    UISettingsSaveBool(Setting_AutoSleep, true);
    printf("mainwindow: auto sleep\n");

    // ---- the System and Options menu with the game running ----
    {
        QtMainMenu * Menu = Window.Menu();
        // pause / resume
        CHECK(Menu->ProcessMessage(ID_SYSTEM_PAUSE));
        CHECK(WaitFor(Paused, 5000));
        CHECK(Menu->ProcessMessage(ID_SYSTEM_PAUSE));
        CHECK(WaitFor([]() { return !Paused(); }, 5000));
        // limit FPS
        const bool Limit = g_Settings->LoadBool(GameRunning_LimitFPS);
        CHECK(Menu->ProcessMessage(ID_SYSTEM_LIMITFPS));
        CHECK(g_Settings->LoadBool(GameRunning_LimitFPS) == !Limit);
        CHECK(Menu->ProcessMessage(ID_SYSTEM_LIMITFPS));
        CHECK(g_Settings->LoadBool(GameRunning_LimitFPS) == Limit);
        // speed
        CHECK(Menu->ProcessMessage(ID_OPTIONS_INCREASE_SPEED));
        CHECK(Menu->ProcessMessage(ID_OPTIONS_DECREASE_SPEED));
        // the save slot
        CHECK(Menu->ProcessMessage(ID_CURRENT_SAVE_3));
        CHECK(g_Settings->LoadDword(Game_CurrentSaveState) == 3);
        // save state and restore: what the game had in memory comes back
        const uint32_t Address = 0x80401000;
        CHECK(Debugger->DebugStore_VAddr<uint32_t>(Address, 0x11111111));
        CHECK(Menu->ProcessMessage(ID_SYSTEM_SAVE));
        Pump(1500);
        CHECK(Debugger->DebugStore_VAddr<uint32_t>(Address, 0x22222222));
        CHECK(Menu->ProcessMessage(ID_SYSTEM_RESTORE));
        uint32_t Value = 0;
        CHECK(WaitFor([&]() { return Debugger->DebugLoad_VAddr(Address, Value) && Value == 0x11111111; }, 10000));
        CHECK(Running());
        // a soft and a hard reset: the game goes on
        CHECK(Menu->ProcessMessage(ID_SYSTEM_RESET_SOFT));
        Pump(1500);
        CHECK(Running());
        CHECK(Menu->ProcessMessage(ID_SYSTEM_RESET_HARD));
        Pump(2500);
        CHECK(Running());
        CHECK(!Paused());
        printf("mainwindow: system menu\n");
    }

    // ---- dialogs leave the full screen ----
    Window.SetFullScreenRequested();
    CHECK(Window.isFullScreen());
    CHECK(UISettingsLoadBool(UserInterface_InFullScreen));
    g_Notify->DisplayError("an error while in the full screen");
    CHECK(WaitFor([&]() { return !Window.isFullScreen(); }, 5000));
    Pump(300);
    CHECK(!UISettingsLoadBool(UserInterface_InFullScreen));
    CHECK(Stack->size() == QSize(640, 480)); // the fixed video area again
    // leaving the full screen when the focus is lost and the setting asks for it
    UISettingsSaveBool(UserInterface_ExitFullscreenOnLoseFocus, true);
    Window.SetFullScreenRequested();
    CHECK(Window.isFullScreen());
    Window.ApplicationActiveChanged(false);
    CHECK(WaitFor([&]() { return !Window.isFullScreen(); }, 5000));
    Window.ApplicationActiveChanged(true);
    UISettingsSaveBool(UserInterface_ExitFullscreenOnLoseFocus, false);
    printf("mainwindow: full screen\n");

    // ---- a dropped file runs (WM_DROPFILES) ----
    g_BaseSystem->CloseCpu();
    CHECK(WaitFor([&]() { return !Running() && Window.RomBrowserShown(); }, 15000));
    CHECK(Window.DropFiles({QUrl::fromLocalFile(Rom)}));
    CHECK(!Window.DropFiles({QUrl("https://example.org/x.z64")}));
    CHECK(WaitFor(Running, 30000));
    Pump(800);
    CHECK(!Window.RomBrowserShown());
    printf("mainwindow: drop\n");

    // ---- the game ends: the browser is back with its place and size, the panes are empty ----
    Window.Menu()->ProcessMessage(ID_FILE_ENDEMULATION);
    CHECK(WaitFor([&]() { return !Running() && Window.RomBrowserShown(); }, 15000));
    Pump(500);
    if (Stack->size() != BrowserSize) printf("browser after the game %dx%d, before %dx%d\n", Stack->width(), Stack->height(), BrowserSize.width(), BrowserSize.height());
    CHECK(Stack->size() == BrowserSize);
    // what the core says when it stops ("Emulation ended") comes after the panes were cleared
    CHECK(Window.StatusPane(1)->text().isEmpty());
    CHECK(Window.StatusPane(0)->text() != "timed message");
    CHECK(!Window.ScreenSaver()->Wanted());
    printf("mainwindow: browser back\n");

    // ---- a ROM inside a 7z archive ("archive.7z?file"), when the test was given one (7z of the
    // host: MAINWINDOW_TEST_7Z=<archive>?<file>) ----
    if (const char * Archive = getenv("MAINWINDOW_TEST_7Z"))
    {
        Window.OpenImage(QString::fromLocal8Bit(Archive));
        CHECK(WaitFor(Running, 30000));
        Pump(800);
        CHECK(Running());
        CHECK(!Window.RomBrowserShown());
        printf("mainwindow: ROM from a 7z archive runs\n");
        Window.Menu()->ProcessMessage(ID_FILE_ENDEMULATION);
        CHECK(WaitFor([&]() { return !Running() && Window.RomBrowserShown(); }, 15000));
    }
    else
    {
        printf("mainwindow: no 7z archive given (MAINWINDOW_TEST_7Z): not tested\n");
    }

    // ---- Discord rich presence: the setting starts and stops the client, the game and the
    // closing of the game (the script action) are sent as activities ----
    {
        // at the start the window sent the handshake and "Not in-game" (CDiscord::Init, Update(false))
        CHECK(WaitFor([&]() { return Discord.All().find("704794684387491891") != std::string::npos; }, 15000));
        CHECK(WaitFor([&]() { return Discord.All().find("\"details\":\"Not in-game\"") != std::string::npos; }, 15000));
        CHECK(Discord.Connections == 1);
        const size_t BeforeGame = Discord.Frames.size();
        Window.OpenImage(Rom);
        CHECK(WaitFor(Running, 30000));
        // the title of CDiscord's GetTitle: the good name of the ROM database, else the file name
        std::string Default;
        const std::string Good = g_Settings->LoadStringVal(Rdb_GoodName, Default) ? g_Settings->LoadStringVal(Rdb_GoodName) : std::string(CPath(g_Settings->LoadStringVal(Game_File)).GetName());
        const QString Title = QString::fromStdString(Good);
        CHECK(!Title.isEmpty());
        auto Since = [&](size_t First, const std::string & Text) {
            std::lock_guard<std::mutex> Guard(Discord.Lock);
            for (size_t i = First; i < Discord.Frames.size(); i++)
            {
                if (Discord.Frames[i].second.find(Text) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        };
        // the game: its title, the start time and Project64's icon
        if (!WaitFor([&]() { return Since(BeforeGame, "\"details\":\"" + Title.toStdString() + "\""); }, 15000))
        {
            printf("title '%s'; frames:\n%s\n", Title.toUtf8().constData(), Discord.All().c_str());
            g_Failures++;
        }
        CHECK(Since(BeforeGame, "\"timestamps\":{\"start\":"));
        CHECK(Since(BeforeGame, "\"large_image\":\"pj64_icon\""));
        Pump(500);
        const size_t BeforeClose = Discord.Frames.size();
        Debugger->ScriptAction(JSAPI_ACT_CLOSE_ROM, 0); // pj64.close() of a script
        CHECK(WaitFor([&]() { return !Running(); }, 15000));
        CHECK(WaitFor([&]() { return Since(BeforeClose, "\"details\":\"Not in-game\""); }, 15000));
        // switched off: the client is stopped and the connection closed; switched on again it
        // connects again and shows the presence
        UISettingsSaveBool(Setting_EnableDiscordRPC, false);
        CHECK(WaitFor([&]() { return Discord.Disconnections >= 1; }, 15000));
        const size_t BeforeAgain = Discord.Frames.size();
        UISettingsSaveBool(Setting_EnableDiscordRPC, true);
        CHECK(WaitFor([&]() { return Discord.Connections >= 2 && Since(BeforeAgain, "SET_ACTIVITY"); }, 20000));
        UISettingsSaveBool(Setting_EnableDiscordRPC, false);
        CHECK(WaitFor([&]() { return Discord.Disconnections >= 2; }, 15000));
        printf("mainwindow: discord presence\n");
    }

    // ---- the library is always the face of the window: the setting of the classic browser does not hide it ----
    UISettingsSaveBool(RomBrowser_Enabled, false);
    Pump(300);
    CHECK(Window.RomBrowserShown());
    UISettingsSaveBool(RomBrowser_Enabled, true);

    Window.close();
    Pump(300);
    CN64System::CloseSystem();
    delete Debugger;
    AppCleanup();
    printf(g_Failures == 0 ? "PASS\n" : "FAILED (%d)\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
