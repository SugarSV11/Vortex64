#pragma once
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <mutex>

// The emulation stands still while a window of configuration is open (the game must not walk on while the user sets the emulator up).
// ONE mechanism for all the windows: a window keeps a ConfigPause for as long as it is open (on the stack around its exec(): the
// destructor runs with Accept, Cancel, the X of the window, an exception or an early return).
//
// The pauses nest: the first one pauses the emulation (SysEvent_PauseCPU_Settings), the following ones only count, and only the
// release of the last one resumes (SysEvent_ResumeCPU_Settings), so a dialog opened by Settings does not resume the game under it when
// it closes. The core does not take that pause when the game is already paused (by the user, from the menu: another kind of pause), and
// does not resume a pause that is not its own kind, so a game that the user paused stays paused.
class ConfigPause
{
public:
    ConfigPause()
    {
        std::lock_guard<std::mutex> Lock(Mutex());
        if (Depth()++ == 0 && g_BaseSystem != nullptr)
        {
            g_BaseSystem->ExternalEvent(SysEvent_PauseCPU_Settings);
        }
    }
    ~ConfigPause()
    {
        std::lock_guard<std::mutex> Lock(Mutex());
        if (--Depth() == 0 && g_BaseSystem != nullptr)
        {
            g_BaseSystem->ExternalEvent(SysEvent_ResumeCPU_Settings);
        }
    }
    ConfigPause(const ConfigPause &) = delete;
    ConfigPause & operator=(const ConfigPause &) = delete;

    // how many windows of configuration are open now
    static int OpenCount()
    {
        std::lock_guard<std::mutex> Lock(Mutex());
        return Depth();
    }

private:
    static std::mutex & Mutex()
    {
        static std::mutex Instance;
        return Instance;
    }
    static int & Depth()
    {
        static int Instance = 0;
        return Instance;
    }
};
