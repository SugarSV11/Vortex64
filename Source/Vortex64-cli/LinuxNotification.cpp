#include "LinuxNotification.h"
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <Vortex64-core/Multilanguage/Language.h>
#include <Vortex64-core/N64System/N64System.h>
#include <Vortex64-core/N64System/SystemGlobals.h>

#include <cstdio>

void CLinuxNotification::DisplayError(const char * Message) const
{
    fprintf(stderr, "\n[ERROR] %s\n", Message ? Message : "");
}

void CLinuxNotification::DisplayError(LanguageStringID StringID) const
{
    DisplayError(GS(StringID));
}

// CNotificationImp::FatalError ends the calling thread (ExitThread); the main thread is left to
// the caller
void CLinuxNotification::FatalError(const char * Message) const
{
    fprintf(stderr, "\n[FATAL] %s\n", Message ? Message : "");
    if (getpid() != (pid_t)syscall(SYS_gettid))
    {
        pthread_exit(nullptr);
    }
}

void CLinuxNotification::FatalError(LanguageStringID StringID) const
{
    FatalError(GS(StringID));
}

void CLinuxNotification::DisplayWarning(const char * Message) const
{
    fprintf(stderr, "\n[WARNING] %s\n", Message ? Message : "");
}

void CLinuxNotification::DisplayWarning(LanguageStringID StringID) const
{
    DisplayWarning(GS(StringID));
}

void CLinuxNotification::DisplayMessage(int DisplayTime, const char * Message) const
{
    (void)DisplayTime;
    printf("[MESSAGE] %s\n", Message ? Message : "");
}

void CLinuxNotification::DisplayMessage(int DisplayTime, LanguageStringID StringID) const
{
    DisplayMessage(DisplayTime, GS(StringID));
}

void CLinuxNotification::DisplayMessage2(const char * Message) const
{
    printf("[STATUS] %s\n", Message ? Message : "");
}

bool CLinuxNotification::AskYesNoQuestion(const char * Question) const
{
    printf("[QUESTION] %s [y/N]: ", Question ? Question : "");
    fflush(stdout);

    char Answer[16] = {};
    if (fgets(Answer, sizeof(Answer), stdin) == nullptr)
    {
        return false;
    }
    return Answer[0] == 'y' || Answer[0] == 'Y';
}

void CLinuxNotification::BreakPoint(const char * FileName, int32_t LineNumber)
{
    // No native debugger UI on this frontend (see g_DebugSettings.haveDebugger
    // users on Windows in Project64/UserInterface/Notification.cpp), so this
    // always takes the "no debugger attached" path: stop emulation cleanly
    // instead of continuing to run against whatever the caller was about to
    // do when it decided something unexpected/unimplemented happened. A
    // silent, non-halting BreakPoint() is what let an incomplete JIT opcode
    // (see Vortex64-cli/main.cpp's ForceInterpreterCPU comment) corrupt a
    // compiled block instead of failing loud, so this matters beyond just
    // matching Windows' behavior.
    fprintf(stderr, "\n[BREAKPOINT] %s:%d - stopping emulation\n", FileName ? FileName : "", LineNumber);
    if (g_BaseSystem)
    {
        g_BaseSystem->CloseCpu();
    }
}

void CLinuxNotification::AppInitDone(void)
{
    printf("[Vortex64] AppInit done.\n");
}

bool CLinuxNotification::ProcessGuiMessages(void) const
{
    // No native GUI message loop in the CLI frontend; the video plugin
    // pumps its own SDL events (see Vortex64-video/Main.cpp).
    return true;
}

void CLinuxNotification::ChangeFullScreen(void) const
{
    printf("[Vortex64] ChangeFullScreen requested.\n");
}
