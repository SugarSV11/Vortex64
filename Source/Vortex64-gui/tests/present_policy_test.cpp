// The decisions of the synchronization of the presentation with the monitor, without a game or a monitor:
//  - the policy (Auto / On / Off) for every refresh rate, game rate and state: a swap interval, never a speed;
//  - REFRESH RATE != EMULATION SPEED: the speed limiter keeps the rate of the game whatever refresh rate has been decided on;
//  - MAXIMUM FPS: the frame cap (deadlines of a monotonic clock, nothing is slept or invented), its values, its limits;
//  - what the frontend reads of a screen (a believable rate or none, the screen that holds the window, "1920×1080 @ 100 Hz").
// Offscreen Qt. The part that needs a real game and plugins is present-sync-test; the part that needs the user's monitor is manual.
#include "../PresentSync.h"

#include <Vortex64-core/N64System/SpeedLimiter.h>
#include <Vortex64-plugin-spec/PresentControl.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <cstdlib>

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

using PJ64Present::ResolveSwapInterval;


// ---- MAXIMUM FPS: the frames of a game that arrive at a rate (with jitter), through the cap, on a clock that the test moves
struct CapRun
{
    int Frames = 0, Presented = 0;
    double MinGapMs = 1e9, MaxGapMs = 0, PresentedPerSecond = 0;
};
static CapRun RunCap(double GameFps, int CapFps, bool Unthrottled, double Seconds, double JitterPercent, int64_t StartNs = 1000000000LL)
{
    PJ64Present::FrameCap Cap;
    CapRun Result;
    const double Period = 1e9 / GameFps;
    uint32_t Seed = 12345;
    int64_t Last = -1;
    double T = 0;
    while (T < Seconds * 1e9)
    {
        Seed = Seed * 1664525u + 1013904223u;
        const double Jitter = ((Seed >> 8) / (double)(1 << 24) - 0.5) * 2.0 * Period * JitterPercent / 100.0;
        const int64_t Now = StartNs + (int64_t)(T + Jitter);
        Result.Frames++;
        if (Cap.Admit(Now, CapFps * 1000, Unthrottled))
        {
            Result.Presented++;
            if (Last >= 0 && Result.Presented > 2) // (the first two frames of a limit may be close: that is the one frame of credit)
            {
                const double Gap = (Now - Last) / 1e6;
                Result.MinGapMs = std::min(Result.MinGapMs, Gap);
                Result.MaxGapMs = std::max(Result.MaxGapMs, Gap);
            }
            Last = Now;
        }
        T += Period;
    }
    Result.PresentedPerSecond = Result.Presented / Seconds;
    return Result;
}

static void MaximumFpsTests()
{
    // the values of the setting: nothing but 0 or 10..1000 gets through
    CHECK(Vortex::PresentSync::ValidMaxFps(0) == 0 && Vortex::PresentSync::ValidMaxFps(10) == 10 && Vortex::PresentSync::ValidMaxFps(1000) == 1000 && Vortex::PresentSync::ValidMaxFps(37) == 37);
    for (int64_t Bad : {-1LL, -100000LL, 1LL, 9LL, 1001LL, 100000LL, 4294967295LL, 4294967296LL, 9223372036854775807LL, (-9223372036854775807LL - 1)})
    {
        CHECK(Vortex::PresentSync::ValidMaxFps(Bad) == 0, "%lld is not a limit", (long long)Bad);
    }
    // a limit of 0 or a hint without a limit presents everything
    {
        const CapRun R = RunCap(60, 0, false, 10, 5);
        CHECK(R.Presented == R.Frames, "no limit: every frame");
    }
    const int Caps[] = {20, 30, 50, 60, 75, 90, 100, 120, 144, 165, 240, 10, 37, 1000};
    for (double Game : {60.0, 50.0, 30.0, 59.94})
    {
        for (int Cap : Caps)
        {
            const CapRun R = RunCap(Game, Cap, false, 20, 4);
            const double Expected = std::min<double>(Game, Cap);
            // never above the limit, never above what the game made (nothing is invented), and the limit is reached
            CHECK(R.Presented <= R.Frames, "%.2f fps, cap %d: no more frames than the game made", Game, Cap);
            CHECK(R.PresentedPerSecond <= Cap * 1.02 + 0.2, "%.2f fps game, cap %d: %.2f presented/s is above the cap", Game, Cap, R.PresentedPerSecond);
            CHECK(std::fabs(R.PresentedPerSecond - Expected) <= Expected * 0.03 + 0.2, "%.2f fps game, cap %d: %.2f presented/s, expected %.2f", Game, Cap, R.PresentedPerSecond, Expected);
            if (Cap >= Game * 1.001)
            {
                CHECK(R.Presented == R.Frames, "%.2f fps game, cap %d (above the game): every frame, not one held back (%d of %d)", Game, Cap, R.Presented, R.Frames);
            }
            if (Cap < Game * 0.95 && Cap >= 10)
            {
                // even pacing: no burst of two frames close together, no hole of more than one period of the limit and one frame
                const double CapPeriodMs = 1000.0 / Cap, GamePeriodMs = 1000.0 / Game;
                CHECK(R.MinGapMs >= CapPeriodMs * 0.5 - GamePeriodMs * 0.1, "%.2f fps game, cap %d: two frames %.2f ms apart (period %.2f)", Game, Cap, R.MinGapMs, CapPeriodMs);
                CHECK(R.MaxGapMs <= CapPeriodMs + GamePeriodMs * 1.3, "%.2f fps game, cap %d: a hole of %.2f ms (period %.2f)", Game, Cap, R.MaxGapMs, CapPeriodMs);
            }
        }
    }
    // a game of 30 FPS with a maximum of 144 stays at 30 (no frame is made up)
    {
        const CapRun R = RunCap(30, 144, false, 10, 3);
        CHECK(R.Presented == R.Frames && std::fabs(R.PresentedPerSecond - 30) < 0.5, "30 fps with a cap of 144 is 30 fps (%.2f)", R.PresentedPerSecond);
    }
    // fast forward: the limit is not applied, whatever the rate of the frames
    for (int Cap : {20, 60, 144})
    {
        const CapRun R = RunCap(600, Cap, true, 5, 10);
        CHECK(R.Presented == R.Frames, "unthrottled, cap %d: every frame (%d of %d)", Cap, R.Presented, R.Frames);
    }
    // the limit does not wait: Admit answers at once and the clock is a parameter (a frame that is not admitted is not slept for)
    {
        PJ64Present::FrameCap Cap;
        QElapsedTimer Clock;
        Clock.start();
        int Held = 0;
        for (int i = 0; i < 1000000; i++)
        {
            Held += Cap.Admit(1000 + i * 10, 60000, false) ? 0 : 1; // a million frames 10 ns apart
        }
        CHECK(Clock.elapsed() < 500 && Held > 999000, "no wait inside Admit (%lld ms, %d held)", (long long)Clock.elapsed(), Held);
    }
    // a stall (a pause, a long frame): no burst afterwards, and the rate comes back without drift
    {
        PJ64Present::FrameCap Cap;
        const int64_t P = 1000000000LL / 30;
        int64_t Now = 5000000000LL;
        for (int i = 0; i < 60; i++, Now += 1000000000LL / 60)
        {
            Cap.Admit(Now, 30000, false);
        }
        Now += 7000000000LL; // seven seconds without frames
        int Burst = 0;
        int64_t Last = -1, MinGap = 1LL << 60;
        for (int i = 0; i < 12; i++, Now += 1000000000LL / 60)
        {
            if (Cap.Admit(Now, 30000, false))
            {
                Burst++;
                if (Last >= 0 && Burst > 2)
                {
                    MinGap = std::min(MinGap, Now - Last);
                }
                Last = Now;
            }
        }
        CHECK(Burst >= 6 && Burst <= 7 && MinGap >= P - P / 4, "after a stall: %d frames in 12, closest gap %lld ns", Burst, (long long)MinGap);
    }
    // a change of the limit while the frames run takes effect at once
    {
        PJ64Present::FrameCap Cap;
        int64_t Now = 1000000000LL;
        int A = 0, B = 0;
        for (int i = 0; i < 120; i++, Now += 1000000000LL / 60)
        {
            A += Cap.Admit(Now, 20000, false) ? 1 : 0;
        }
        for (int i = 0; i < 120; i++, Now += 1000000000LL / 60)
        {
            B += Cap.Admit(Now, 60000, false) ? 1 : 0;
        }
        CHECK(std::abs(A - 40) <= 2 && B == 120, "20 then 60: %d, %d", A, B);
    }
    // what a plugin keeps: the cap comes with the hint, counts what it presented and what it held back, and the swap interval is the same
    // whatever the cap (the cap and the vertical sync are two decisions)
    for (int Mode : {PJ64_VSYNC_AUTO, PJ64_VSYNC_ON, PJ64_VSYNC_OFF})
    {
        for (int Hz : {60, 75, 100, 120, 144, 165, 240})
        {
            int Interval = 99;
            for (int Cap : {0, 20, 30, 50, 60, 75, 90, 100, 120, 144, 165, 240, 37})
            {
                PJ64Present::Control C;
                PJ64_PRESENT_HINT H = {Mode, Hz * 1000, 60000, 0, Cap * 1000};
                C.Set(H);
                const int Got = C.Resolve(false, true).Interval;
                CHECK(Interval == 99 || Got == Interval, "mode %d, %d Hz: cap %d changed the swap interval (%d, was %d)", Mode, Hz, Cap, Got, Interval);
                Interval = Got;
                // the presented rate of a 60 fps game is min(60, cap) in every mode and on every monitor (VSync is the swap interval, not a frame rate)
                int Presented = 0;
                int64_t Now = 1000000000LL;
                for (int i = 0; i < 600; i++, Now += 1000000000LL / 60)
                {
                    Presented += C.Admit(Now) ? 1 : 0;
                }
                const double Expected = Cap == 0 ? 60.0 : std::min(60.0, (double)Cap);
                CHECK(std::fabs(Presented / 10.0 - Expected) <= Expected * 0.03 + 0.2, "mode %d, %d Hz, cap %d: %.1f presented/s (expected %.1f)", Mode, Hz, Cap, Presented / 10.0, Expected);
            }
        }
    }
    {
        PJ64Present::Control C;
        PJ64_PRESENT_HINT H = {PJ64_VSYNC_OFF, 100000, 60000, 0, 30000};
        C.Set(H);
        int64_t Now = 1000000000LL;
        for (int i = 0; i < 600; i++, Now += 1000000000LL / 60)
        {
            C.Admit(Now);
        }
        PJ64_PRESENT_STATUS S = {};
        C.FillRecorded(S);
        CHECK(S.MaxMilliFps == 30000 && S.Presented + S.Skipped == 600 && std::llabs(S.Presented - 300) <= 3, "status: cap %d, presented %lld, held back %lld", S.MaxMilliFps, (long long)S.Presented, (long long)S.Skipped);
        H.Unthrottled = 1;
        C.Set(H);
        for (int i = 0; i < 100; i++, Now += 1000000000LL / 60)
        {
            C.Admit(Now);
        }
        PJ64_PRESENT_STATUS F = {};
        C.FillRecorded(F);
        CHECK(F.Skipped == S.Skipped && F.Presented == S.Presented + 100, "fast forward: the cap is not applied (%lld held back)", (long long)(F.Skipped - S.Skipped));
        // without a hint: no cap
        PJ64Present::Control Bare;
        int Shown = 0;
        for (int i = 0; i < 100; i++)
        {
            Shown += Bare.Admit(1000000000LL + i * 1000) ? 1 : 0;
        }
        CHECK(Shown == 100, "without a frontend nothing is held back");
    }
}

int main(int argc, char ** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication App(argc, argv);

    // ---- the policy
    const int Rates[] = {30, 50, 59, 60, 75, 100, 120, 144, 165, 240};
    for (int Game : {60, 50})
    {
        for (int Hz : Rates)
        {
            for (bool Adaptive : {false, true})
            {
                const int G = Game * 1000, R = Hz * 1000;
                CHECK(ResolveSwapInterval(PJ64_VSYNC_OFF, R, G, false, Adaptive).Interval == 0, "off is 0 at %d Hz", Hz);
                CHECK(ResolveSwapInterval(PJ64_VSYNC_ON, R, G, false, Adaptive).Interval == 1, "on is 1 at %d Hz", Hz);
                CHECK(ResolveSwapInterval(PJ64_VSYNC_ON, R, G, true, Adaptive).Interval == 1, "on is 1 unthrottled too");
                CHECK(ResolveSwapInterval(PJ64_VSYNC_OFF, R, G, true, Adaptive).Interval == 0);
                const int Auto = ResolveSwapInterval(PJ64_VSYNC_AUTO, R, G, false, Adaptive).Interval;
                if (Hz >= Game * 0.98)
                {
                    CHECK(Auto == 1, "auto, %d Hz monitor, %d fps game: vertical sync (%d)", Hz, Game, Auto);
                }
                else
                {
                    CHECK(Auto == (Adaptive ? -1 : 0), "auto, %d Hz monitor, %d fps game: never waits for a slower monitor (%d)", Hz, Game, Auto);
                }
                CHECK(ResolveSwapInterval(PJ64_VSYNC_AUTO, R, G, true, Adaptive).Interval == 0, "auto, unthrottled: immediate");
            }
        }
        // a monitor of 59.94 Hz holds a 60 fps game; a refresh rate that is not known is not 60 Hz made up
        CHECK(ResolveSwapInterval(PJ64_VSYNC_AUTO, 59940, 60000, false, false).Interval == 1);
        CHECK(ResolveSwapInterval(PJ64_VSYNC_AUTO, 0, Game * 1000, false, true).Interval == -1 && ResolveSwapInterval(PJ64_VSYNC_AUTO, 0, Game * 1000, false, false).Interval == 1, "unknown rate: adaptive when there is, else one refresh");
    }
    // the same plan whatever else changes: only the inputs of the policy matter
    CHECK(ResolveSwapInterval(PJ64_VSYNC_AUTO, 100000, 60000, false, true).Interval == ResolveSwapInterval(PJ64_VSYNC_AUTO, 144000, 60000, false, true).Interval);

    // ---- what a plugin keeps: no hint, its own setting; with a hint, the hint
    {
        PJ64Present::Control C;
        CHECK(!C.Hinted() && C.Resolve(true, false).Interval == 1 && C.Resolve(true, true).Interval == -1 && C.Resolve(false, true).Interval == 0, "without a frontend the plugin's own box decides");
        PJ64_PRESENT_HINT H = {PJ64_VSYNC_OFF, 144000, 60000, 0};
        C.Set(H);
        CHECK(C.Hinted() && C.Resolve(true, true).Interval == 0, "with a hint the old box does not count");
        H.Mode = PJ64_VSYNC_AUTO;
        C.Set(H);
        CHECK(C.Resolve(false, false).Interval == 1);
        C.Record(1, 1, true);
        PJ64_PRESENT_STATUS S = {};
        C.FillRecorded(S);
        CHECK(S.Hinted == 1 && S.Wanted == 1 && S.Applied == 1 && S.AdaptiveOk == 1 && S.RefreshMilliHz == 144000 && S.GameMilliHz == 60000);
    }

    MaximumFpsTests();

    // ---- REFRESH RATE != EMULATION SPEED: the limiter of the core runs the game at its own rate, whatever has been decided about
    // the monitor (it has no input from the monitor: the policy gives an interval, the limiter gives the speed)
    for (int Hz : {60, 75, 100, 144, 165, 240})
    {
        const PJ64Present::Plan Plan = ResolveSwapInterval(PJ64_VSYNC_AUTO, Hz * 1000, 60000, false, true);
        (void)Plan;
        CSpeedLimiter Limiter;
        Limiter.SetHertz(60);
        QElapsedTimer Clock;
        uint32_t Rate = 0;
        Limiter.Timer_Process(&Rate); // (the first call only starts the clock)
        Clock.start();
        int Frames = 0;
        while (Clock.elapsed() < 600)
        {
            Limiter.Timer_Process(&Rate);
            Frames++;
        }
        const double Fps = Frames * 1000.0 / Clock.elapsed();
        CHECK(Limiter.GetSpeed() == 60 && Limiter.GetBaseSpeed() == 60, "the speed of the limiter at a %d Hz monitor", Hz);
        CHECK(Fps > 57 && Fps < 62, "%d Hz monitor: the limiter runs the game at %.1f fps", Hz, Fps);
    }

    // ---- what the frontend reads of the screens
    CHECK(Vortex::PresentSync::SaneRefresh(0) == 0 && Vortex::PresentSync::SaneRefresh(-1) == 0 && Vortex::PresentSync::SaneRefresh(5) == 0 && Vortex::PresentSync::SaneRefresh(5000) == 0, "implausible rates are not believed");
    CHECK(Vortex::PresentSync::SaneRefresh(59.94) == 59.94 && Vortex::PresentSync::SaneRefresh(144) == 144 && Vortex::PresentSync::SaneRefresh(240) == 240);
    {
        Vortex::DisplayInfo I;
        I.Size = QSize(1920, 1080);
        I.RefreshHz = 100;
        CHECK(I.Text() == QString::fromUtf8("1920×1080 @ 100 Hz"), "'%s'", I.Text().toUtf8().constData());
        I.RefreshHz = 59.94;
        CHECK(I.Text() == QString::fromUtf8("1920×1080 @ 59.94 Hz"), "'%s'", I.Text().toUtf8().constData());
        I.RefreshHz = 164.999;
        CHECK(I.Text() == QString::fromUtf8("1920×1080 @ 165 Hz"), "'%s'", I.Text().toUtf8().constData());
        I.RefreshHz = 0;
        CHECK(I.Text() == QString::fromUtf8("1920×1080"), "an unknown rate says nothing: '%s'", I.Text().toUtf8().constData());
    }
    QScreen * Screen = QGuiApplication::primaryScreen();
    CHECK(Screen != nullptr);
    if (Screen != nullptr)
    {
        const QList<QScreen *> Screens = QGuiApplication::screens();
        const QRect Geometry = Screen->geometry();
        CHECK(Vortex::PresentSync::ScreenHolding(Geometry.adjusted(10, 10, -10, -10), Screens, nullptr) == Screen, "a window inside the screen");
        CHECK(Vortex::PresentSync::ScreenHolding(Geometry.translated(Geometry.width() - 20, 0), Screens, nullptr) == Screen, "a window mostly outside is still on the only screen that touches it");
        CHECK(Vortex::PresentSync::ScreenHolding(Geometry.translated(Geometry.width() * 3, 0), Screens, Screen) == Screen && Vortex::PresentSync::ScreenHolding(Geometry.translated(Geometry.width() * 3, 0), Screens, nullptr) == nullptr, "on no screen: the fallback");
        CHECK(Vortex::PresentSync::ScreenHolding(Geometry, QList<QScreen *>(), Screen) == Screen);
        qunsetenv("VORTEX_FAKE_REFRESH");
        const Vortex::DisplayInfo Real = Vortex::PresentSync::Describe(Screen);
        CHECK(Real.RefreshHz == Vortex::PresentSync::SaneRefresh(Screen->refreshRate()) && !Real.Fake, "what Qt says, nothing made up (%f)", Real.RefreshHz);
        for (int Hz : {60, 75, 100, 120, 144, 165, 240})
        {
            qputenv("VORTEX_FAKE_REFRESH", QByteArray::number(Hz));
            const Vortex::DisplayInfo Fake = Vortex::PresentSync::Describe(Screen);
            CHECK(Fake.RefreshHz == Hz && Fake.Fake && Fake.Size == Real.Size, "VORTEX_FAKE_REFRESH=%d", Hz);
        }
        qunsetenv("VORTEX_FAKE_REFRESH");
    }
    printf(g_Failures == 0 ? "PASS present-policy (%d checks)\n" : "FAILED present-policy (%d checks)\n", g_Checks);
    return g_Failures == 0 ? 0 : 1;
}
