// How the frontend and the video plugins agree on the synchronization of the PRESENTATION with the monitor.
//
// The frontend (Vortex64) tells the plugin, with the optional export PJ64SetPresentHint:
//   - the mode the user chose: Auto, On or Off;
//   - the refresh rate of the monitor that holds the window (0: not known);
//   - the frame rate of the game (the VI of the console: 60 or 50 normally);
//   - whether the emulation runs unthrottled (fast forward, "limit FPS" off).
// and the plugin tells what it applied, with the optional export PJ64GetPresentStatus.
//
// "Maximum FPS" (MaxMilliFps of the hint, 0 = automatic) limits how often a finished frame is PRESENTED, nothing else: a frame that comes
// before its turn is not swapped (the plugin has drawn it, the screen keeps the previous one). The emulation is never made to wait
// for it (no sleep, no busy loop in the thread of the game): the game goes on at its own speed, the audio too, and no frame is invented
// (a game of 30 FPS with a maximum of 144 stays at 30). The turns are deadlines of a monotonic clock (FrameCap), so the rate does not
// drift. While the emulation runs unthrottled (fast forward) the limit is not applied.
//
// The refresh rate of the monitor is NEVER the speed of the emulation: the speed is the speed limiter of the core (wall clock,
// the VI of the game). The only thing this decides is the swap interval of the OpenGL context, that is, when a finished frame
// is shown. The decision is a pure function (ResolveSwapInterval) shared by both plugins and by the tests.
//
// Header only, no dependencies: the plugins are built apart from the core.
#pragma once
#include <atomic>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <stdint.h>

#define PJ64_VSYNC_AUTO 0
#define PJ64_VSYNC_ON 1
#define PJ64_VSYNC_OFF 2

#ifdef __cplusplus
extern "C" {
#endif
typedef struct
{
    int32_t Mode;           // PJ64_VSYNC_*
    int32_t RefreshMilliHz; // refresh rate of the monitor in thousandths of Hz, 0 when it is not known
    int32_t GameMilliHz;    // frame rate of the game in thousandths of Hz (60000, 50000...), 0 when it is not known
    int32_t Unthrottled;    // not 0: the emulation runs faster than the game (or without limit)
    int32_t MaxMilliFps;    // maximum presentation rate in thousandths of FPS, 0: automatic (no limit of its own)
} PJ64_PRESENT_HINT;

typedef struct
{
    int32_t Hinted;       // the frontend has sent a hint (otherwise the plugin follows its own setting)
    int32_t Mode;         // the mode in use: PJ64_VSYNC_*
    int32_t Wanted;       // the swap interval the policy asks for: 0 immediate, 1 vertical sync, -1 adaptive
    int32_t Applied;      // the swap interval the context really has (0 when the driver refused the others)
    int32_t AdaptiveOk;   // the context accepts the adaptive interval (EXT_swap_control_tear)
    int32_t RefreshMilliHz;
    int32_t GameMilliHz;
    int32_t Unthrottled;
    int32_t MaxMilliFps;  // the limit in use (0: automatic)
    int64_t Presented;    // frames swapped since the plugin started
    int64_t Skipped;      // frames that the limit did not present
} PJ64_PRESENT_STATUS;
#ifdef __cplusplus
}

namespace PJ64Present
{
struct Plan
{
    int Interval;       // 0 immediate, 1 vertical sync, -1 adaptive vertical sync
    const char * Reason;
};

// Auto:
//   unthrottled emulation        -> 0   (a wait for the monitor would slow the fast forward down)
//   the monitor is not known     -> -1  (adaptive: a late frame is shown at once, so a slow monitor cannot slow the game; else 1)
//   monitor >= game rate (98 %)  -> 1   (vertical sync: no tearing; the frames are shown at the refreshes, the game keeps its speed)
//   monitor slower than the game -> -1  (or 0): waiting for every refresh would slow the game down, so a late frame is shown at once
// On: 1 always (the user asked for it; a monitor slower than the game does slow it down). Off: 0.
inline Plan ResolveSwapInterval(int Mode, int RefreshMilliHz, int GameMilliHz, bool Unthrottled, bool AdaptiveOk)
{
    if (Mode == PJ64_VSYNC_OFF)
    {
        return {0, "off"};
    }
    if (Mode == PJ64_VSYNC_ON)
    {
        return {1, "on"};
    }
    if (Unthrottled)
    {
        return {0, "auto: the emulation is not throttled"};
    }
    if (RefreshMilliHz <= 0)
    {
        return AdaptiveOk ? Plan{-1, "auto: the refresh rate is not known (adaptive)"} : Plan{1, "auto: the refresh rate is not known"};
    }
    if (GameMilliHz <= 0 || (int64_t)RefreshMilliHz * 100 >= (int64_t)GameMilliHz * 98)
    {
        return {1, "auto: the monitor is at least as fast as the game"};
    }
    return AdaptiveOk ? Plan{-1, "auto: the monitor is slower than the game (adaptive)"} : Plan{0, "auto: the monitor is slower than the game"};
}

// The turns of the presentation. Admit() says whether the frame that is ready now is to be presented. Pure: the clock is a parameter.
//   - no limit (0) or an emulation without throttle: every frame, and the deadline is forgotten;
//   - otherwise the frames are admitted on a grid of deadlines one period apart: a frame that comes up to one period before its
//     deadline is admitted (the jitter of a game that runs at the same rate as the limit must not halve it), the next deadline is the
//     previous one plus the period (no drift), and after a stall (a pause, a long frame) the grid starts again from now: at most one frame
//     of credit (two close frames), never a burst.
class FrameCap
{
public:
    bool Admit(int64_t NowNs, int32_t MaxMilliFps, bool Unthrottled)
    {
        if (MaxMilliFps <= 0 || Unthrottled)
        {
            m_Next = 0;
            m_MilliFps = 0;
            return true;
        }
        if (MaxMilliFps != m_MilliFps || m_Next == 0)
        {
            m_MilliFps = MaxMilliFps;
            m_Next = NowNs; // (the first frame of a limit is presented)
        }
        const int64_t Period = 1000000000000LL / MaxMilliFps;
        // (a cell-rate rule: a frame is admitted when it is at most one period before its deadline, so a frame that was late and the one that
        // follows it close behind are both shown, and the mean never goes over the limit)
        if (NowNs + Period < m_Next)
        {
            return false;
        }
        m_Next = std::max(m_Next, NowNs) + Period;
        return true;
    }

private:
    int64_t m_Next = 0;
    int32_t m_MilliFps = 0;
};

// What a plugin keeps: written by the thread of the frontend, read by the thread that swaps. The hint is one piece (a mutex, taken once per
// swap): the thread that swaps never sees the mode of one hint with the refresh rate of another.
class Control
{
public:
    void Set(const PJ64_PRESENT_HINT & Hint)
    {
        std::lock_guard<std::mutex> Lock(m_Lock);
        m_Hint = Hint;
        m_Hinted = true;
    }
    bool Hinted() const
    {
        std::lock_guard<std::mutex> Lock(m_Lock);
        return m_Hinted;
    }
    // The plan for the moment. Without a hint the plugin follows its own setting (LegacyOn): the old checkbox.
    Plan Resolve(bool LegacyOn, bool AdaptiveOk) const
    {
        std::lock_guard<std::mutex> Lock(m_Lock);
        if (!m_Hinted)
        {
            return LegacyOn ? Plan{AdaptiveOk ? -1 : 1, "no frontend: the setting of the plugin"} : Plan{0, "no frontend: the setting of the plugin"};
        }
        return ResolveSwapInterval(m_Hint.Mode, m_Hint.RefreshMilliHz, m_Hint.GameMilliHz, m_Hint.Unthrottled != 0, AdaptiveOk);
    }
    // Is the frame that is ready now to be presented? (thread of the swap; counts what it presented and what it skipped)
    bool Admit(int64_t NowNs)
    {
        std::lock_guard<std::mutex> Lock(m_Lock);
        const bool Present = m_Cap.Admit(NowNs, m_Hinted ? m_Hint.MaxMilliFps : 0, m_Hinted && m_Hint.Unthrottled != 0);
        (Present ? m_Presented : m_Skipped)++;
        return Present;
    }
    bool Admit()
    {
        return Admit(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    // what the swap thread last applied (the thread that swaps writes, the frontend reads)
    void Record(int Wanted, int Applied, bool AdaptiveOk)
    {
        std::lock_guard<std::mutex> Lock(m_Lock);
        m_Wanted = Wanted;
        m_Applied = Applied;
        m_AdaptiveOk = AdaptiveOk;
    }
    void FillRecorded(PJ64_PRESENT_STATUS & Status) const
    {
        std::lock_guard<std::mutex> Lock(m_Lock);
        Status.Hinted = m_Hinted ? 1 : 0;
        Status.Mode = m_Hint.Mode;
        Status.Wanted = m_Wanted;
        Status.Applied = m_Applied;
        Status.AdaptiveOk = m_AdaptiveOk ? 1 : 0;
        Status.RefreshMilliHz = m_Hint.RefreshMilliHz;
        Status.GameMilliHz = m_Hint.GameMilliHz;
        Status.Unthrottled = m_Hint.Unthrottled != 0 ? 1 : 0;
        Status.MaxMilliFps = m_Hint.MaxMilliFps;
        Status.Presented = m_Presented;
        Status.Skipped = m_Skipped;
    }

private:
    mutable std::mutex m_Lock;
    PJ64_PRESENT_HINT m_Hint = {PJ64_VSYNC_AUTO, 0, 0, 0, 0};
    FrameCap m_Cap;
    int64_t m_Presented = 0, m_Skipped = 0;
    bool m_Hinted = false, m_AdaptiveOk = false;
    int m_Wanted = 0, m_Applied = 0;
};
} // namespace PJ64Present
#endif
