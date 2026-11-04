// Lifecycle test for the real Qt main window with a real ROM and the real plugins.
// Needs an X server (Scripts/linux/run-gui-tests.sh starts Xvfb) because the video
// plugin embeds its SDL/OpenGL window into the main window.
//
//   lifecycle-test <rom> [cycles]
//
// Each cycle: open the ROM, wait until it runs, pause/resume, save state, soft reset,
// load state, end emulation. Then the recompiler is selected and a ROM opened once
// more (upstream's x64 recompiler stops at the first unimplemented opcode; the test
// only requires that this ends cleanly). Finally the window is closed.
#include "../MainMenu.h"
#include "../MainWindow.h"

#include <Common/path.h>
#include <Project64-core/AppInit.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/Settings.h>
#include <Project64/Settings/UISettings.h>

#include <QApplication>
#include <QMessageBox>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <cstdio>
#include <unistd.h>
#include <cstdlib>
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

static int g_ErrorDialogs = 0;

// A modal error box (e.g. the BreakPoint of an unimplemented JIT opcode) would block the
// nested event loop of exec(); accept it like a user pressing OK and count it.
static void DismissDialogs()
{
    for (QWidget * Widget : QApplication::topLevelWidgets())
    {
        QMessageBox * Box = qobject_cast<QMessageBox *>(Widget);
        if (Box != nullptr && Box->isVisible())
        {
            g_ErrorDialogs++;
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
        DismissDialogs();
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

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        printf("usage: %s <rom> [cycles]\n", argv[0]);
        return 2;
    }
    const QString Rom = QString::fromLocal8Bit(argv[1]);
    const int Cycles = argc > 2 ? atoi(argv[2]) : 3;
    setenv("SDL_VIDEODRIVER", "x11", 0);
    QApplication App(argc, argv);
    MainWindow Window;
    // a modal box runs its own event loop, so a timer has to be the one that closes it
    QTimer DismissTimer;
    QObject::connect(&DismissTimer, &QTimer::timeout, &DismissDialogs);
    DismissTimer.start(50);
    const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
    char * Args[] = {argv[0], nullptr};
    if (!AppInit(Window.GetNotify(), Base.c_str(), 1, Args))
    {
        return 2;
    }
    Window.CreateLanguageMenus();
    Window.InitPlugins();
    Window.show();
    Pump(500);
    CHECK(g_Plugins->Gfx() != nullptr && g_Plugins->Audio() != nullptr && g_Plugins->Control() != nullptr && g_Plugins->RSP() != nullptr);
    QtMainMenu * Menu = Window.Menu();
    CHECK(Menu != nullptr);
    // LIFECYCLE_JIT=1: the cycles run on the recompiler (the interpreter is the default); an error
    // dialog (a break point of the recompiler, an unhandled opcode) is then a failure
    const bool Jit = getenv("LIFECYCLE_JIT") != nullptr;
    g_Settings->SaveBool(Setting_ForceInterpreterCPU, !Jit);
    if (Jit)
    {
        g_Settings->SaveDword(Game_CpuType, CPU_Recompiler);
        // LIFECYCLE_FUNCFIND=<0|1|2>: how the recompiler finds a function (the physical or the virtual
        // lookup), LIFECYCLE_SMMFUNC=<0|1>: the check of the code of a compiled function
        if (getenv("LIFECYCLE_FUNCFIND") != nullptr)
        {
            g_Settings->SaveDword(Game_FuncLookupMode, (uint32_t)atoi(getenv("LIFECYCLE_FUNCFIND")));
        }
        if (getenv("LIFECYCLE_SMMFUNC") != nullptr)
        {
            g_Settings->SaveBool(Game_SMM_ValidFunc, atoi(getenv("LIFECYCLE_SMMFUNC")) != 0);
        }
    }

    // LIFECYCLE_BADPLUGIN=1: plugins that are missing or are not plugins; the game does not start, the application
    // says so and stays alive, and with the plugins back the game runs
    if (getenv("LIFECYCLE_BADPLUGIN") != nullptr)
    {
        const std::string Good = g_Settings->LoadStringVal(Plugin_GFX_Current);
        const std::string GoodAudio = g_Settings->LoadStringVal(Plugin_AUDIO_Current);
        const std::string Dir = g_Settings->LoadStringVal(Directory_Plugin);
        // not a plugin: a text file with the name of one
        const std::string Fake = Dir + "libFakePlugin.so";
        FILE * F = fopen(Fake.c_str(), "w");
        if (F != nullptr)
        {
            fputs("not an ELF file", F);
            fclose(F);
        }
        const char * Bad[] = {"libDoesNotExist.so", "libFakePlugin.so"};
        for (const char * Name : Bad)
        {
            g_Settings->SaveString(Plugin_GFX_Current, Name);
            g_Settings->SaveString(Plugin_AUDIO_Current, Name);
            const int DialogsBefore = g_ErrorDialogs;
            Window.OpenImage(Rom);
            Pump(4000);
            printf("badplugin %s: running=%d dialogs=%d\n", Name, (int)Running(), g_ErrorDialogs - DialogsBefore);
            CHECK(!Running());
        }
        unlink(Fake.c_str());
        g_Settings->SaveString(Plugin_GFX_Current, Good);
        g_Settings->SaveString(Plugin_AUDIO_Current, GoodAudio);
        Window.OpenImage(Rom);
        CHECK(WaitFor(Running, 30000));
        Pump(1500);
        CHECK(Running());
        printf("badplugin: recovered, running=%d\n", (int)Running());
        Menu->ProcessMessage(ID_FILE_ENDEMULATION);
        CHECK(WaitFor([]() { return !Running(); }, 15000));
        Pump(500);
    }

    // LIFECYCLE_RESTART=1: the game is loaded without starting it, started, ended, and started again (File > Start Emulation)
    if (getenv("LIFECYCLE_RESTART") != nullptr)
    {
        g_Settings->SaveBool(Setting_AutoStart, false);
        // (the library always starts what it opens, so the core is asked directly for a game that waits)
        CHECK(CN64System::RunFileImage(Rom.toUtf8().constData()));
        Pump(2500);
        CHECK(!Running());
        for (int i = 0; i < 3; i++)
        {
            Menu->ProcessMessage(ID_FILE_STARTEMULATION);
            CHECK(WaitFor(Running, 30000));
            Pump(2000);
            CHECK(Running());
            Menu->ProcessMessage(ID_FILE_ENDEMULATION);
            CHECK(WaitFor([]() { return !Running(); }, 30000));
            Pump(500);
            printf("restart %d: ok\n", i + 1);
        }
        g_Settings->SaveBool(Setting_AutoStart, true);
    }

    // LIFECYCLE_STRESS=<n>: the game is started and ended n times at once (the end comes 0 to 400 ms after the start,
    // while the emulation thread and the plugins are still starting)
    if (getenv("LIFECYCLE_STRESS") != nullptr)
    {
        const int Times = atoi(getenv("LIFECYCLE_STRESS"));
        for (int i = 0; i < Times; i++)
        {
            Window.OpenImage(Rom);
            Pump((i * 53) % 400);
            Menu->ProcessMessage(ID_FILE_ENDEMULATION);
            CHECK(WaitFor([]() { return !Running(); }, 20000));
            Pump(100);
        }
        printf("stress: %d starts and ends, dialogs=%d\n", Times, (int)g_ErrorDialogs);
        CHECK(g_ErrorDialogs == 0);
    }

    for (int Cycle = 0; Cycle < Cycles; Cycle++)
    {
        printf("cycle %d: open\n", Cycle + 1);
        Window.OpenImage(Rom);
        CHECK(WaitFor(Running, 30000));
        Pump(2500);
        CHECK(g_BaseSystem != nullptr);
        // LIFECYCLE_SOAK=<seconds>: the game just runs (the first cycle) and has to go on without error
        if (Cycle == 0 && getenv("LIFECYCLE_SOAK") != nullptr)
        {
            const int Seconds = atoi(getenv("LIFECYCLE_SOAK"));
            for (int i = 0; i < Seconds; i++)
            {
                Pump(1000);
                if (!Running() || g_ErrorDialogs != 0)
                {
                    break;
                }
            }
            printf("soak: %d s, running=%d dialogs=%d\n", Seconds, (int)Running(), (int)g_ErrorDialogs);
            CHECK(Running());
            CHECK(g_ErrorDialogs == 0);
        }

        Menu->ProcessMessage(ID_SYSTEM_PAUSE);
        CHECK(WaitFor(Paused, 5000));
        Menu->ProcessMessage(ID_SYSTEM_PAUSE);
        CHECK(WaitFor([]() { return !Paused(); }, 5000));

        Menu->ProcessMessage(ID_CURRENT_SAVE_3);
        Menu->ProcessMessage(ID_SYSTEM_SAVE);
        Pump(1500);
        Menu->ProcessMessage(ID_SYSTEM_RESET_SOFT);
        Pump(1500);
        CHECK(Running());
        Menu->ProcessMessage(ID_SYSTEM_RESTORE);
        Pump(1500);
        CHECK(Running());

        if (Cycle == 0)
        {
            // Changing the audio plugin while the game runs must not stop it (upstream
            // resets the plugins through SysEvent_ChangePlugins)
            const std::string Dir = g_Settings->LoadStringVal(Directory_Plugin);
            const std::string Alt = Dir + "libProject64-audio-alt.so";
            unlink(Alt.c_str());
            CHECK(symlink((Dir + "libProject64-audio.so").c_str(), Alt.c_str()) == 0);
            g_Settings->SaveString(Plugin_AUDIO_Current, "libProject64-audio-alt.so");
            Pump(4000);
            CHECK(Running());
            CHECK(g_Plugins->Audio() != nullptr);
            g_Settings->SaveString(Plugin_AUDIO_Current, "libProject64-audio.so");
            Pump(4000);
            CHECK(Running());
            CHECK(g_Plugins->Audio() != nullptr && g_Plugins->Gfx() != nullptr);
            unlink(Alt.c_str());
            printf("plugin change while running: ok\n");
        }

        Menu->ProcessMessage(ID_FILE_ENDEMULATION);
        CHECK(WaitFor([]() { return !Running(); }, 15000));
        Pump(500);
        if (Jit)
        {
            CHECK(g_ErrorDialogs == 0);
        }
    }

    // Recompiler selected: must not crash the process; it may stop on an unimplemented opcode
    printf("recompiler pass\n");
    g_Settings->SaveBool(Setting_ForceInterpreterCPU, false);
    g_Settings->SaveDword(Game_CpuType, CPU_Recompiler);
    Window.OpenImage(Rom);
    printf("recompiler: opened\n");
    WaitFor(Running, 30000);
    Pump(4000);
    printf("recompiler: running=%d\n", (int)Running());
    if (Running())
    {
        Menu->ProcessMessage(ID_FILE_ENDEMULATION);
    }
    CHECK(WaitFor([]() { return !Running(); }, 15000));
    printf("error dialogs seen: %d\n", g_ErrorDialogs);
    g_Settings->SaveBool(Setting_ForceInterpreterCPU, true);

    printf("recompiler: ended\n");
    // One more interpreter run, then close the window with the ROM running
    Window.OpenImage(Rom);
    CHECK(WaitFor(Running, 30000));
    Pump(1500);
    printf("closing window\n");
    Window.close();
    Pump(500);
    printf("closed\n");
    CHECK(!Running());
    AppCleanup();
    printf(g_Failures == 0 ? "PASS\n" : "FAILED\n");
    return g_Failures == 0 ? 0 : 1;
}
