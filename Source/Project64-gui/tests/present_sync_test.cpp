// The synchronization of the presentation with the monitor, with a real ROM and one video plugin (Glide64 or GLideN64):
//   present-sync-test <rom> [plugin library]
//
// REFRESH RATE != EMULATION SPEED: whatever the refresh rate of the monitor (60, 75, 100, 120, 144, 165, 240 Hz, given with
// VORTEX_FAKE_REFRESH, which replaces what Qt reads) and whatever the mode (Auto, On, Off), the game keeps the speed of the console
// (the VI/s the speed limiter measures never go above the game's rate, the speed of the limiter is the base one), the plugin gets the
// hint (mode, refresh, game rate) and applies the swap interval that the policy asks for. It also checks the unthrottled emulation, that
// a change of monitor is taken without making the plugin again, and windowed -> full screen -> windowed several times (the interval
// is the same afterwards, the game runs). Needs an X server and a window manager (Scripts/linux/run-gui-tests.sh).
// MAXIMUM FPS (the setting of Graphics) limits the presentation only: with every limit (automatic, 20 ... 240, a custom one), every mode and
// monitor, the VI/s and the audio the game produces per second stay what they were, the frames presented per second follow min(limit, what the
// game makes), and fast forward ignores the limit.
// What cannot be seen here - the smoothness of the picture, the tearing, a real monitor, a second one - is the manual test.
#include "../MainMenu.h"
#include "../MainWindow.h"
#include "../PresentSync.h"

#include <Common/path.h>
#include <Project64-core/AppInit.h>
#include <Project64-core/N64System/MemoryHandler/AudioInterfaceHandler.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Plugins/GFXPlugin.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/Settings.h>
#include <Project64/Settings/UISettings.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QMessageBox>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

static int g_Failures = 0, g_Checks = 0;
#define CHECK(cond, ...)                                                       \
    do                                                                         \
    {                                                                          \
        g_Checks++;                                                            \
        if (!(cond))                                                           \
        {                                                                      \
            printf("FAIL %s:%d: %s ", __FILE__, __LINE__, #cond);              \
            printf("" __VA_ARGS__);                                            \
            printf("\n");                                                      \
            g_Failures++;                                                      \
        }                                                                      \
    } while (0)

static int g_ErrorDialogs = 0;

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

// VI/s of the emulator over a stretch of wall clock time: the VI counter of the console, not the speed limiter's own count
static double ViRate(int Ms)
{
    const uint64_t Before = g_BaseSystem->ViCount();
    QElapsedTimer Clock;
    Clock.start();
    Pump(Ms);
    return (g_BaseSystem->ViCount() - Before) * 1000.0 / Clock.elapsed();
}

static PJ64_PRESENT_STATUS Status()
{
    PJ64_PRESENT_STATUS Result = {};
    g_Plugins->GetPresentStatus(Result);
    return Result;
}

// Full screen on or off through the menu. The window manager of the test (Xvfb has none of its own) answers late now and then, as in the
// full screen test of GLideN64: a request that was not answered is made once more.
static bool SetFullScreen(MainWindow & Window, QtMainMenu * Menu, bool On)
{
    if (Window.isFullScreen() == On)
    {
        return true;
    }
    Menu->ProcessMessage(ID_OPTIONS_FULLSCREEN);
    if (WaitFor([&]() { return Window.isFullScreen() == On; }, 6000))
    {
        return true;
    }
    printf("fullscreen: no answer to the request, asking again\n");
    if (Window.isFullScreen() != On)
    {
        Menu->ProcessMessage(ID_OPTIONS_FULLSCREEN);
    }
    return WaitFor([&]() { return Window.isFullScreen() == On; }, 8000);
}

static const char * ModeName(int Mode)
{
    return Mode == PJ64_VSYNC_ON ? "on" : (Mode == PJ64_VSYNC_OFF ? "off" : "auto");
}

// the monitor of the test: the refresh rate the frontend sees, and one pass of the loop that tells the plugin
static void SetMonitor(int Hz, int Mode)
{
    qputenv("VORTEX_FAKE_REFRESH", QByteArray::number(Hz));
    g_Settings->SaveDword(UserInterface_VSyncMode, (uint32_t)Mode);
    Vortex::PresentSync::Instance()->Refresh();
}

// the frontend's setting "Maximum FPS" (0: automatic) and one pass of the loop that tells the plugin
static void SetCap(int Fps)
{
    g_Settings->SaveDword(UserInterface_MaxFps, (uint32_t)Fps);
    Vortex::PresentSync::Instance()->Refresh();
}

// what the game and the presentation did over a stretch of wall clock time (per second)
struct Sample
{
    double Vi = 0, Audio = 0, Presented = 0, Skipped = 0;
};
static Sample Measure(int Ms)
{
    const uint64_t Vi = g_BaseSystem->ViCount(), Audio = AudioBytesProduced();
    const PJ64_PRESENT_STATUS Before = Status();
    QElapsedTimer Clock;
    Clock.start();
    Pump(Ms);
    const double Seconds = Clock.elapsed() / 1000.0;
    const PJ64_PRESENT_STATUS After = Status();
    Sample Result;
    Result.Vi = (g_BaseSystem->ViCount() - Vi) / Seconds;
    Result.Audio = (AudioBytesProduced() - Audio) / Seconds;
    Result.Presented = (After.Presented - Before.Presented) / Seconds;
    Result.Skipped = (After.Skipped - Before.Skipped) / Seconds;
    return Result;
}

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        printf("usage: %s <rom> [plugin library]\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const QString Rom = QString::fromLocal8Bit(argv[1]);
    const std::string VideoPlugin = argc > 2 ? argv[2] : "libGLideN64.so";
    setenv("SDL_VIDEODRIVER", "x11", 0);
    QApplication App(argc, argv);
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
    g_Settings->SaveString(Plugin_GFX_Current, VideoPlugin);
    g_Settings->SaveBool(Setting_ForceInterpreterCPU, true);
    UISettingsSaveBool(Setting_AutoSleep, false);
    g_Settings->SaveBool((SettingID)Setting_AutoFullscreen, false);
    Window.CreateLanguageMenus();
    Window.InitPlugins();
    Window.show();
    Pump(500);
    CHECK(g_Plugins->Gfx() != nullptr);
    if (g_Plugins->Gfx() == nullptr)
    {
        printf("FAILED\n");
        return 1;
    }
    printf("plugin: %s\n", g_Plugins->Gfx()->PluginName());
    CHECK(g_Plugins->Gfx()->PJ64SetPresentHint != nullptr && g_Plugins->Gfx()->PJ64GetPresentStatus != nullptr, "the plugin exports the hint");
    Vortex::PresentSync * Sync = Vortex::PresentSync::Instance();
    CHECK(Sync != nullptr);
    if (Sync == nullptr)
    {
        printf("FAILED\n");
        return 1;
    }
    QtMainMenu * Menu = Window.Menu();

    // ---- the monitor as the frontend sees it: no Hz is made up
    qunsetenv("VORTEX_FAKE_REFRESH");
    Sync->Refresh();
    const Vortex::DisplayInfo Real = Sync->Display();
    printf("display: %s %s\n", Real.Name.toUtf8().constData(), Real.Text().toUtf8().constData());
    CHECK(Real.Size.width() > 0 && Real.Size.height() > 0, "the size of the monitor is known");
    CHECK(Real.RefreshHz == 0 || (Real.RefreshHz >= 20 && Real.RefreshHz <= 1000), "a rate is a believable one or not known (%f)", Real.RefreshHz);

    Window.OpenImage(Rom);
    CHECK(WaitFor(Running, 30000));
    Pump(2500);
    CHECK(Running() && g_BaseSystem != nullptr);
    if (!Running() || g_BaseSystem == nullptr)
    {
        printf("FAILED\n");
        return 1;
    }
    const int BaseSpeed = g_BaseSystem->GetBaseSpeed();
    const uint32_t GameHz = g_Settings->LoadDword(GameRunning_ScreenHertz);
    CHECK(GameHz == (uint32_t)BaseSpeed && GameHz > 0, "the rate of the game is the VI of the console (%u)", GameHz);
    // the speed that this machine gives the game with the monitor of the first configuration: what the others are compared with
    SetMonitor(60, PJ64_VSYNC_OFF);
    Pump(1000);
    const double Reference = ViRate(3000);
    printf("reference: %.1f VI/s (game %u)\n", Reference, GameHz);
    CHECK(Reference <= GameHz * 1.05 + 1 && Reference > GameHz * 0.5, "the reference is about the speed of the game (%.1f)", Reference);

    // ---- refresh != speed: every rate and every mode
    const int Rates[] = {60, 75, 100, 120, 144, 165, 240};
    const int Modes[] = {PJ64_VSYNC_AUTO, PJ64_VSYNC_ON, PJ64_VSYNC_OFF};
    const void * PluginBefore = g_Plugins->Gfx();
    for (int Mode : Modes)
    {
        for (int Hz : Rates)
        {
            const int HintsBefore = Sync->HintsSent();
            SetMonitor(Hz, Mode);
            Pump(500);
            const double Rate = ViRate(2000);
            const PJ64_PRESENT_STATUS S = Status();
            const PJ64Present::Plan Plan = PJ64Present::ResolveSwapInterval(Mode, Hz * 1000, (int)GameHz * 1000, false, S.AdaptiveOk != 0);
            printf("%s %3d Hz: wanted %2d applied %2d (adaptive %d), %.1f VI/s\n", ModeName(Mode), Hz, S.Wanted, S.Applied, S.AdaptiveOk, Rate);
            CHECK(Sync->Display().RefreshHz == Hz, "the monitor says %d Hz", Hz);
            CHECK(S.Hinted == 1 && S.Mode == Mode && S.RefreshMilliHz == Hz * 1000 && S.GameMilliHz == (int)GameHz * 1000 && S.Unthrottled == 0, "the plugin has the hint (%d %d %d)", S.Mode, S.RefreshMilliHz, S.GameMilliHz);
            CHECK(S.Wanted == Plan.Interval, "%s %d Hz: wanted %d, the policy says %d", ModeName(Mode), Hz, S.Wanted, Plan.Interval);
            // what the context really has: the wanted interval, or what the driver of this X server could give
            CHECK(S.Applied == S.Wanted || (S.Wanted == -1 && S.Applied == 1) || S.Applied == 0, "applied %d for wanted %d", S.Applied, S.Wanted);
            if (Mode == PJ64_VSYNC_OFF)
            {
                CHECK(S.Applied == 0 && S.Wanted == 0, "Off is immediate presentation");
            }
            // the speed of the emulation is the game's: the monitor does not change it
            CHECK(g_BaseSystem->GetSpeed() == BaseSpeed, "the speed of the limiter is the base one (%d)", g_BaseSystem->GetSpeed());
            CHECK(Rate <= GameHz * 1.05 + 1, "%s %d Hz: %.1f VI/s, not faster than the game (%u)", ModeName(Mode), Hz, Rate, GameHz);
            CHECK(Rate >= Reference * 0.7, "%s %d Hz: %.1f VI/s, not slower than the reference (%.1f)", ModeName(Mode), Hz, Rate, Reference);
            CHECK(Sync->HintsSent() > HintsBefore || Hz == 60, "the change was sent to the plugin");
            CHECK(g_Plugins->Gfx() == PluginBefore && Running(), "the plugin was not made again and the game runs");
        }
    }

    // ---- MAXIMUM FPS: the presentation is limited, the game is not
    {
        SetMonitor(100, PJ64_VSYNC_OFF);
        SetCap(0);
        Pump(1000);
        Sample Base = Measure(4000);
        // (a software renderer warms up and its rate wanders: what the game makes is the best of three stretches)
        for (int Again = 0; Again < 2; Again++)
        {
            Base.Presented = std::max(Base.Presented, Measure(3000).Presented);
        }
        printf("maximum fps: automatic: %.1f VI/s, audio %.0f bytes/s, presented %.1f/s, held back %.1f/s\n", Base.Vi, Base.Audio, Base.Presented, Base.Skipped);
        CHECK(Status().MaxMilliFps == 0 && Base.Skipped == 0, "automatic: nothing is held back (%.2f/s)", Base.Skipped);
        CHECK(Base.Presented > 5, "the plugin presents frames (%.1f/s)", Base.Presented);
        const double AudioPerVi = Base.Vi > 0 ? Base.Audio / Base.Vi : 0;
        const int Caps[] = {20, 30, 50, 60, 75, 90, 100, 120, 144, 165, 240, 37 /* custom */, 0};
        for (int Cap : Caps)
        {
            SetCap(Cap);
            Pump(800);
            const Sample M = Measure(3500);
            const PJ64_PRESENT_STATUS S = Status();
            const double Expected = Cap == 0 ? Base.Presented : std::min<double>(Cap, Base.Presented);
            printf("maximum fps %3d: %.1f VI/s, audio %.0f B/s (%.1f B/VI), presented %.1f/s (expected %.1f), held back %.1f/s\n", Cap, M.Vi, M.Audio, M.Vi > 0 ? M.Audio / M.Vi : 0, M.Presented, Expected, M.Skipped);
            CHECK(S.MaxMilliFps == Cap * 1000, "the plugin has the limit (%d)", S.MaxMilliFps);
            // THE POINT: the limit is not the speed of the game
            CHECK(M.Vi <= GameHz * 1.05 + 1 && M.Vi >= Base.Vi * 0.9 && M.Vi <= Base.Vi * 1.1, "cap %d: %.1f VI/s, the game goes at its speed (%.1f without a limit)", Cap, M.Vi, Base.Vi);
            CHECK(g_BaseSystem->GetSpeed() == BaseSpeed, "cap %d: the speed of the limiter is the base one", Cap);
            if (AudioPerVi > 0 && M.Vi > 0)
            {
                CHECK(std::fabs(M.Audio / M.Vi - AudioPerVi) <= AudioPerVi * 0.08, "cap %d: the audio of the game per frame is the same (%.1f, was %.1f): it is not sped up or slowed", Cap, M.Audio / M.Vi, AudioPerVi);
                CHECK(M.Audio <= Base.Audio * 1.1 && M.Audio >= Base.Audio * 0.9, "cap %d: audio %.0f B/s (%.0f without a limit)", Cap, M.Audio, Base.Audio);
            }
            // the presentation: min(limit, what the game makes), never more than the game made
            CHECK(M.Presented <= std::max(Base.Presented, M.Vi) * 1.08 + 1 && (Cap == 0 || M.Presented <= Cap * 1.1 + 1), "cap %d: %.1f presented/s is above the limit or the frames of the game (%.1f VI/s)", Cap, M.Presented, M.Vi);
            // (the lower bound only where the limit is what decides: a software renderer's own rate wanders from stretch to stretch)
            CHECK(Cap == 0 || Cap > Base.Presented * 0.7 || M.Presented >= Cap * 0.8 - 1, "cap %d: %.1f presented/s, expected about %.1f", Cap, M.Presented, Expected);
            // (frames are held back when the plugin offers more than the limit lets through: the offered rate is what it presented plus what it held back)
            CHECK(Cap == 0 || M.Presented + M.Skipped <= Cap * 1.15 || M.Skipped > 0, "cap %d: %.1f offered/s, none held back", Cap, M.Presented + M.Skipped);
            CHECK(Cap == 0 || Cap > Base.Presented * 1.05 ? M.Skipped < 0.5 : true, "cap %d (above what the game makes): no frame is held back (%.2f/s)", Cap, M.Skipped);
            CHECK(Running() && g_Plugins->Gfx() == PluginBefore, "the game runs and the plugin is the same");
        }
        // the limit in every mode and on every monitor: the same
        for (int Mode : Modes)
        {
            for (int Hz : Rates)
            {
                SetMonitor(Hz, Mode);
                SetCap(60);
                Pump(500);
                const Sample M = Measure(1500);
                const PJ64_PRESENT_STATUS S = Status();
                CHECK(S.MaxMilliFps == 60000 && S.Mode == Mode && S.RefreshMilliHz == Hz * 1000, "%s %d Hz: the hint has the limit and the monitor", ModeName(Mode), Hz);
                CHECK(M.Vi <= GameHz * 1.05 + 1 && M.Vi >= Base.Vi * 0.85, "%s %d Hz, limit 60: %.1f VI/s", ModeName(Mode), Hz, M.Vi);
                CHECK(M.Presented <= 60 * 1.1 + 1, "%s %d Hz, limit 60: %.1f presented/s", ModeName(Mode), Hz, M.Presented);
                const PJ64Present::Plan Plan = PJ64Present::ResolveSwapInterval(Mode, Hz * 1000, (int)GameHz * 1000, false, S.AdaptiveOk != 0);
                CHECK(S.Wanted == Plan.Interval, "%s %d Hz, limit 60: the swap interval is the policy's, the limit does not change it (%d, %d)", ModeName(Mode), Hz, S.Wanted, Plan.Interval);
            }
        }
        // a value that the file of settings should not have is automatic, never a limit of 5 fps or of 4 billion
        for (uint32_t Bad : {1u, 9u, 1001u, 100000u, 4294967295u})
        {
            g_Settings->SaveDword(UserInterface_MaxFps, Bad);
            Vortex::PresentSync::Instance()->Refresh();
            CHECK(Vortex::PresentSync::Instance()->MaxFps() == 0 && Status().MaxMilliFps == 0, "%u in the file is automatic", Bad);
        }
        // fast forward: the limit is not applied (nothing is held back while the emulation is not throttled)
        SetMonitor(100, PJ64_VSYNC_AUTO);
        SetCap(20);
        Pump(800);
        for (int i = 0; i < 5; i++)
        {
            g_BaseSystem->AlterSpeed(CSpeedLimiter::INCREASE_SPEED);
        }
        CHECK(WaitFor([]() { return Status().Unthrottled == 1 && Status().MaxMilliFps == 20000; }, 6000), "fast forward with a limit of 20");
        Pump(500);
        const Sample Fast = Measure(2000);
        printf("maximum fps 20 in fast forward: %.1f VI/s, presented %.1f/s, held back %.1f/s\n", Fast.Vi, Fast.Presented, Fast.Skipped);
        CHECK(Fast.Skipped == 0, "fast forward: no frame is held back by the limit (%.2f/s)", Fast.Skipped);
        // (the speed that a software renderer can reach is not asserted: it may be the game's own already; what counts is that the limit held nothing back)
        CHECK(Fast.Presented >= 25, "fast forward: presented %.1f/s, more than the limit of 20", Fast.Presented);
        g_BaseSystem->SetSpeed(BaseSpeed);
        CHECK(WaitFor([]() { return Status().Unthrottled == 0; }, 6000), "back to the normal speed");
        Pump(1000);
        const Sample Normal = Measure(3000);
        CHECK(Normal.Skipped > 0 || Base.Presented <= 21, "the limit is applied again after the fast forward (%.1f held back/s)", Normal.Skipped);
        CHECK(Normal.Vi <= GameHz * 1.05 + 1 && Normal.Vi >= Base.Vi * 0.85, "and the game is at its speed (%.1f VI/s)", Normal.Vi);
        SetCap(0);
        SetMonitor(60, PJ64_VSYNC_OFF);
        Pump(500);
    }

    // ---- the monitor changes while the game runs: taken at once, the renderer stays
    {
        SetMonitor(60, PJ64_VSYNC_AUTO);
        Pump(500);
        const int Changes = Sync->DisplayChanges(), Hints = Sync->HintsSent();
        qputenv("VORTEX_FAKE_REFRESH", "144");
        Pump(700); // (the timer of the frontend sees it: no call needed)
        CHECK(Sync->DisplayChanges() == Changes + 1 && Sync->Display().RefreshHz == 144, "the change of monitor was seen");
        CHECK(Sync->HintsSent() == Hints + 1 && Status().RefreshMilliHz == 144000, "and sent once");
        Pump(1000);
        CHECK(Sync->HintsSent() == Hints + 1, "nothing is sent again while nothing changes");
        CHECK(g_Plugins->Gfx() == PluginBefore && Running());
    }

    // ---- an emulation that is not throttled (fast forward): Auto does not wait for the monitor
    {
        SetMonitor(100, PJ64_VSYNC_AUTO);
        Pump(800);
        const PJ64_PRESENT_STATUS Throttled = Status();
        for (int i = 0; i < 5; i++)
        {
            g_BaseSystem->AlterSpeed(CSpeedLimiter::INCREASE_SPEED);
        }
        WaitFor([]() { return Status().Wanted == 0 && Status().Unthrottled == 1; }, 6000);
        const PJ64_PRESENT_STATUS Fast = Status();
        CHECK(Fast.Unthrottled == 1 && Fast.Wanted == 0, "Auto, fast forward: immediate (%d, %d)", Fast.Unthrottled, Fast.Wanted);
        g_BaseSystem->SetSpeed(BaseSpeed);
        SetMonitor(100, PJ64_VSYNC_ON);
        for (int i = 0; i < 5; i++)
        {
            g_BaseSystem->AlterSpeed(CSpeedLimiter::INCREASE_SPEED);
        }
        WaitFor([]() { return Status().Wanted == 1 && Status().Mode == PJ64_VSYNC_ON && Status().Unthrottled == 1; }, 6000);
        CHECK(Status().Wanted == 1, "On waits for the monitor even then");
        g_BaseSystem->SetSpeed(BaseSpeed);
        SetMonitor(100, PJ64_VSYNC_AUTO);
        WaitFor([&]() { return Status().Unthrottled == 0 && Status().Mode == PJ64_VSYNC_AUTO && Status().Wanted == Throttled.Wanted; }, 6000);
        const PJ64_PRESENT_STATUS Back = Status();
        CHECK(Back.Unthrottled == 0 && Back.Wanted == Throttled.Wanted, "back to the normal speed, back to the interval (%d, %d)", Back.Unthrottled, Back.Wanted);
    }

    // ---- window -> full screen -> window, several times, in every mode: the interval comes back, the game runs at its speed
    for (int Mode : Modes)
    {
        SetMonitor(144, Mode);
        Pump(1500);
        const PJ64_PRESENT_STATUS Before = Status();
        for (int Round = 0; Round < 3; Round++)
        {
            CHECK(SetFullScreen(Window, Menu, true), "full screen");
            Pump(1800);
            const PJ64_PRESENT_STATUS Full = Status();
            CHECK(Running(), "the game runs in full screen");
            CHECK(Full.Hinted == 1 && Full.Wanted == Before.Wanted && Full.Applied == Before.Applied, "%s: full screen keeps the interval (%d/%d)", ModeName(Mode), Full.Wanted, Full.Applied);
            CHECK(Sync->Display().Size.width() > 0, "the monitor of the full screen is known");
            CHECK(SetFullScreen(Window, Menu, false), "window");
            Pump(1800);
            const PJ64_PRESENT_STATUS Back = Status();
            CHECK(Running(), "the game runs in the window again");
            CHECK(Back.Wanted == Before.Wanted && Back.Applied == Before.Applied, "%s: the window has its interval again (%d/%d, was %d/%d)", ModeName(Mode), Back.Wanted, Back.Applied, Before.Wanted, Before.Applied);
            const double After = ViRate(1500);
            CHECK(g_BaseSystem->GetSpeed() == BaseSpeed && After <= GameHz * 1.05 + 1 && After > GameHz * 0.3, "the speed is the game's after the round, never above it (%.1f VI/s; the software renderer of the test is slower in full screen)", After);
        }
        printf("%s: full screen x3 ok (interval %d, applied %d)\n", ModeName(Mode), Before.Wanted, Before.Applied);
    }

    // ---- another video plugin comes (the game is closed and opened with the other one): it starts with the hint of the frontend
    {
        const std::string Other = VideoPlugin == "libGLideN64.so" ? "libProject64-video.so" : "libGLideN64.so";
        SetMonitor(120, PJ64_VSYNC_OFF);
        Pump(500);
        Menu->ProcessMessage(ID_FILE_ENDEMULATION);
        CHECK(WaitFor([]() { return !Running(); }, 15000));
        g_Settings->SaveString(Plugin_GFX_Current, Other);
        Window.OpenImage(Rom);
        CHECK(WaitFor(Running, 30000));
        Pump(2500);
        CHECK(Running() && g_Plugins->Gfx() != nullptr && Other == g_Plugins->Gfx()->PluginName() ? true : (g_Plugins->Gfx() != nullptr && strstr(g_Plugins->Gfx()->PluginName(), Other == "libGLideN64.so" ? "GLideN64" : "Project64") != nullptr), "the other plugin runs");
        const PJ64_PRESENT_STATUS S = Status();
        CHECK(S.Hinted == 1 && S.Mode == PJ64_VSYNC_OFF && S.RefreshMilliHz == 120000 && S.Wanted == 0, "the new plugin has the hint of the frontend, not its own setting (%d %d %d)", S.Hinted, S.Mode, S.RefreshMilliHz);
        SetMonitor(120, PJ64_VSYNC_ON);
        Pump(1500);
        CHECK(Status().Wanted == 1, "and follows the changes");
    }

    CHECK(g_ErrorDialogs == 0, "no error dialog");
    Menu->ProcessMessage(ID_FILE_ENDEMULATION);
    CHECK(WaitFor([]() { return !Running(); }, 15000));
    Pump(500);
    Window.close();
    Pump(500);
    AppCleanup();
    printf(g_Failures == 0 ? "PASS present-sync %s (%d checks)\n" : "FAILED present-sync %s (%d checks)\n", VideoPlugin.c_str(), g_Checks);
    return g_Failures == 0 ? 0 : 1;
}
