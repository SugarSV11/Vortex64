#include "windows.h"

#include <atomic>
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <mutex>
#include <set>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <time.h>

ScriptShimHooks g_ScriptShimHooks = {nullptr, nullptr};

// Handles are tagged so that CloseHandle/WaitForSingleObject know what they are given
namespace
{
enum HandleKind
{
    KindThread,
    KindEvent,
    KindTimer,
    KindPipe,
    KindProcess,
    KindFind
};

struct HandleBase
{
    HandleKind kind;
};

struct ThreadHandle : HandleBase
{
    pthread_t thread;
    bool joined = false;
    ThreadHandle() { kind = KindThread; }
};

// Events and timers share one lock and one condition variable (on CLOCK_MONOTONIC): a wait
// on several of them (the timer and the quit event of a worker) needs a single place to
// sleep, and they are few
struct SyncState
{
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond;
    SyncState()
    {
        pthread_condattr_t attr;
        pthread_condattr_init(&attr);
        pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        pthread_cond_init(&cond, &attr);
        pthread_condattr_destroy(&attr);
    }
};

SyncState & Sync()
{
    static SyncState State;
    return State;
}

struct EventHandle : HandleBase
{
    bool manualReset;
    bool signalled;
    EventHandle()
    {
        kind = KindEvent;
    }
};

struct TimerHandle : HandleBase
{
    bool manualReset = false;
    bool armed = false;
    int64_t dueNs = 0;    // CLOCK_MONOTONIC
    int64_t periodNs = 0; // 0: one shot
    TimerHandle()
    {
        kind = KindTimer;
    }
};

struct PipeHandle : HandleBase
{
    int fd = -1;
    PipeHandle()
    {
        kind = KindPipe;
    }
};

struct ProcessHandle : HandleBase
{
    pid_t pid = -1;
    bool exited = false;
    int status = 0;
    ProcessHandle()
    {
        kind = KindProcess;
    }
};

struct FindHandle : HandleBase
{
    DIR * dir = nullptr;
    std::string pattern;
    FindHandle()
    {
        kind = KindFind;
    }
};

std::atomic<int> g_LiveThreads{0};

// The process groups of the children of CreateProcessA (each child leads its own), for
// ScriptShimTerminateProcesses; a group stays here after its leader was reaped, until no
// process of it is left (the shell of exec("... &") ends, what it started does not). A process
// ID is not reused while a process group with that ID exists.
std::mutex g_ProcessLock;
std::set<pid_t> g_Processes;

bool GroupAlive(pid_t Group)
{
    return killpg(Group, 0) == 0 || errno != ESRCH;
}

// With g_ProcessLock held
void PruneProcessGroups()
{
    for (auto It = g_Processes.begin(); It != g_Processes.end();)
    {
        It = GroupAlive(*It) ? std::next(It) : g_Processes.erase(It);
    }
}

std::set<pid_t> LiveProcessGroups()
{
    std::lock_guard<std::mutex> Guard(g_ProcessLock);
    PruneProcessGroups();
    return g_Processes;
}

int64_t MonotonicNs()
{
    struct timespec Now;
    clock_gettime(CLOCK_MONOTONIC, &Now);
    return (int64_t)Now.tv_sec * 1000000000LL + Now.tv_nsec;
}

struct ThreadStart
{
    DWORD (*proc)(void *);
    void * param;
};

void * ThreadMain(void * Arg)
{
    ThreadStart Start = *(ThreadStart *)Arg;
    delete (ThreadStart *)Arg;
    Start.proc(Start.param);
    g_LiveThreads--;
    return nullptr;
}

void SleepNs(int64_t Ns)
{
    struct timespec Request;
    Request.tv_sec = Ns / 1000000000LL;
    Request.tv_nsec = Ns % 1000000000LL;
    while (nanosleep(&Request, &Request) != 0 && errno == EINTR)
    {
    }
}

// With the sync lock held: is the object signalled? (an auto reset event, a timer that is due
// and is not periodic are reset by the wait that they release)
bool TryAcquire(HandleBase * Base, int64_t Now)
{
    if (Base->kind == KindEvent)
    {
        EventHandle * Event = (EventHandle *)Base;
        if (!Event->signalled)
        {
            return false;
        }
        if (!Event->manualReset)
        {
            Event->signalled = false;
        }
        return true;
    }
    TimerHandle * Timer = (TimerHandle *)Base;
    if (!Timer->armed || Now < Timer->dueNs)
    {
        return false;
    }
    if (Timer->periodNs > 0)
    {
        // the next expiry after now (missed periods are not queued)
        Timer->dueNs += Timer->periodNs * (1 + (Now - Timer->dueNs) / Timer->periodNs);
    }
    else if (!Timer->manualReset)
    {
        Timer->armed = false;
    }
    return true;
}

// Events and timers: the first one of Handles that is signalled (WaitAll is not supported:
// the script system never waits for all of several objects)
DWORD WaitSync(DWORD Count, const HANDLE * Handles, DWORD Milliseconds)
{
    SyncState & State = Sync();
    const int64_t Deadline = Milliseconds == INFINITE ? INT64_MAX : MonotonicNs() + (int64_t)Milliseconds * 1000000LL;
    pthread_mutex_lock(&State.lock);
    for (;;)
    {
        const int64_t Now = MonotonicNs();
        int64_t Next = Deadline;
        for (DWORD i = 0; i < Count; i++)
        {
            HandleBase * Base = (HandleBase *)Handles[i];
            if (TryAcquire(Base, Now))
            {
                pthread_mutex_unlock(&State.lock);
                return WAIT_OBJECT_0 + i;
            }
            if (Base->kind == KindTimer && ((TimerHandle *)Base)->armed && ((TimerHandle *)Base)->dueNs < Next)
            {
                Next = ((TimerHandle *)Base)->dueNs;
            }
        }
        if (Milliseconds != INFINITE && Now >= Deadline)
        {
            pthread_mutex_unlock(&State.lock);
            return WAIT_TIMEOUT;
        }
        if (Next == INT64_MAX)
        {
            pthread_cond_wait(&State.cond, &State.lock);
        }
        else
        {
            struct timespec Until;
            Until.tv_sec = Next / 1000000000LL;
            Until.tv_nsec = Next % 1000000000LL;
            pthread_cond_timedwait(&State.cond, &State.lock, &Until);
        }
    }
}

// A finished child is reaped once (its exit status is kept in the handle)
void ReapProcess(ProcessHandle * Process, int Status)
{
    Process->status = Status;
    Process->exited = true;
    std::lock_guard<std::mutex> Guard(g_ProcessLock);
    PruneProcessGroups();
}
} // namespace

int ScriptShimLiveThreads()
{
    return g_LiveThreads;
}

// Ends the processes that scripts started (exec() waits for them, so a script cannot be
// stopped while one is running): their process groups get SIGTERM, and SIGKILL if something is
// still there after half a second
void ScriptShimTerminateProcesses()
{
    for (pid_t Group : LiveProcessGroups())
    {
        killpg(Group, SIGTERM);
    }
    for (int i = 0; i < 50; i++)
    {
        if (LiveProcessGroups().empty())
        {
            return;
        }
        SleepNs(10000000);
    }
    for (pid_t Group : LiveProcessGroups())
    {
        killpg(Group, SIGKILL);
    }
}

HANDLE CreateThread(void *, size_t, DWORD (*StartAddress)(void *), void * Parameter, DWORD, DWORD * ThreadId)
{
    ThreadHandle * Handle = new ThreadHandle;
    ThreadStart * Start = new ThreadStart{StartAddress, Parameter};
    g_LiveThreads++;
    if (pthread_create(&Handle->thread, nullptr, ThreadMain, Start) != 0)
    {
        g_LiveThreads--;
        delete Start;
        delete Handle;
        return nullptr;
    }
    if (ThreadId != nullptr)
    {
        *ThreadId = (DWORD)(uintptr_t)Handle->thread;
    }
    return Handle;
}

HANDLE CreateEvent(void *, BOOL ManualReset, BOOL InitialState, const char *)
{
    EventHandle * Handle = new EventHandle;
    Handle->manualReset = ManualReset != 0;
    Handle->signalled = InitialState != 0;
    return Handle;
}

BOOL SetEvent(HANDLE Event)
{
    SyncState & State = Sync();
    pthread_mutex_lock(&State.lock);
    ((EventHandle *)Event)->signalled = true;
    pthread_cond_broadcast(&State.cond);
    pthread_mutex_unlock(&State.lock);
    return TRUE;
}

BOOL ResetEvent(HANDLE Event)
{
    SyncState & State = Sync();
    pthread_mutex_lock(&State.lock);
    ((EventHandle *)Event)->signalled = false;
    pthread_mutex_unlock(&State.lock);
    return TRUE;
}

DWORD WaitForSingleObject(HANDLE Object, DWORD Milliseconds)
{
    HandleBase * Base = (HandleBase *)Object;
    if (Base->kind == KindThread)
    {
        ThreadHandle * Handle = (ThreadHandle *)Base;
        if (!Handle->joined)
        {
            if (Milliseconds == INFINITE)
            {
                pthread_join(Handle->thread, nullptr);
            }
            else
            {
                struct timespec Until;
                clock_gettime(CLOCK_REALTIME, &Until);
                Until.tv_sec += Milliseconds / 1000;
                Until.tv_nsec += (Milliseconds % 1000) * 1000000L;
                if (Until.tv_nsec >= 1000000000L)
                {
                    Until.tv_sec += 1;
                    Until.tv_nsec -= 1000000000L;
                }
                if (pthread_timedjoin_np(Handle->thread, nullptr, &Until) != 0)
                {
                    return WAIT_TIMEOUT;
                }
            }
            Handle->joined = true;
        }
        return WAIT_OBJECT_0;
    }
    if (Base->kind == KindProcess)
    {
        ProcessHandle * Process = (ProcessHandle *)Base;
        const int64_t Deadline = Milliseconds == INFINITE ? INT64_MAX : MonotonicNs() + (int64_t)Milliseconds * 1000000LL;
        while (!Process->exited)
        {
            int Status = 0;
            const pid_t Result = waitpid(Process->pid, &Status, Milliseconds == INFINITE ? 0 : WNOHANG);
            if (Result == Process->pid)
            {
                ReapProcess(Process, Status);
            }
            else if (Result < 0 && errno != EINTR)
            {
                ReapProcess(Process, 0);
            }
            else if (Result == 0)
            {
                if (MonotonicNs() >= Deadline)
                {
                    return WAIT_TIMEOUT;
                }
                SleepNs(2000000);
            }
        }
        return WAIT_OBJECT_0;
    }
    return WaitSync(1, &Object, Milliseconds);
}

// Events and timers only (the script system waits on a timer/stop event pair, see
// JSIntervalWorker)
DWORD WaitForMultipleObjects(DWORD Count, const HANDLE * Handles, BOOL WaitAll, DWORD Milliseconds)
{
    (void)WaitAll;
    return WaitSync(Count, Handles, Milliseconds);
}

BOOL CloseHandle(HANDLE Object)
{
    if (Object == nullptr || Object == INVALID_HANDLE_VALUE)
    {
        return FALSE;
    }
    HandleBase * Base = (HandleBase *)Object;
    if (Base->kind == KindThread)
    {
        ThreadHandle * Handle = (ThreadHandle *)Base;
        if (!Handle->joined)
        {
            pthread_detach(Handle->thread);
        }
        delete Handle;
    }
    else if (Base->kind == KindTimer)
    {
        delete (TimerHandle *)Base;
    }
    else if (Base->kind == KindPipe)
    {
        PipeHandle * Handle = (PipeHandle *)Base;
        if (Handle->fd >= 0)
        {
            close(Handle->fd);
        }
        delete Handle;
    }
    else if (Base->kind == KindProcess)
    {
        ProcessHandle * Handle = (ProcessHandle *)Base;
        if (!Handle->exited)
        {
            // the process keeps running after its handle is closed: something has to reap it
            const pid_t Pid = Handle->pid;
            std::thread([Pid]() {
                int Status;
                while (waitpid(Pid, &Status, 0) < 0 && errno == EINTR)
                {
                }
                std::lock_guard<std::mutex> Guard(g_ProcessLock);
                PruneProcessGroups();
            }).detach();
        }
        delete Handle;
    }
    else if (Base->kind == KindFind)
    {
        FindHandle * Handle = (FindHandle *)Base;
        if (Handle->dir != nullptr)
        {
            closedir(Handle->dir);
        }
        delete Handle;
    }
    else
    {
        delete (EventHandle *)Base;
    }
    return TRUE;
}

void Sleep(DWORD Milliseconds)
{
    SleepNs((int64_t)Milliseconds * 1000000LL);
}

HANDLE CreateWaitableTimer(void *, BOOL ManualReset, const char *)
{
    TimerHandle * Timer = new TimerHandle;
    Timer->manualReset = ManualReset != 0;
    return Timer;
}

BOOL SetWaitableTimer(HANDLE Handle, const LARGE_INTEGER * DueTime, long Period, void *, void *, BOOL)
{
    TimerHandle * Timer = (TimerHandle *)Handle;
    SyncState & State = Sync();
    // negative: relative time in 100 ns units
    const int64_t RelativeNs = DueTime->QuadPart < 0 ? -DueTime->QuadPart * 100 : 0;
    pthread_mutex_lock(&State.lock);
    Timer->dueNs = MonotonicNs() + RelativeNs;
    Timer->periodNs = (int64_t)Period * 1000000LL;
    Timer->armed = true;
    pthread_cond_broadcast(&State.cond);
    pthread_mutex_unlock(&State.lock);
    return TRUE;
}

void GetSystemTimeAsFileTime(FILETIME * FileTime)
{
    // 100 ns intervals since 1601-01-01
    struct timespec Now;
    clock_gettime(CLOCK_REALTIME, &Now);
    const uint64_t Ticks = ((uint64_t)Now.tv_sec + 11644473600ULL) * 10000000ULL + (uint64_t)Now.tv_nsec / 100;
    FileTime->dwLowDateTime = (DWORD)(Ticks & 0xFFFFFFFF);
    FileTime->dwHighDateTime = (DWORD)(Ticks >> 32);
}

// ---- files ----

namespace
{
bool NextEntry(FindHandle * Find, WIN32_FIND_DATAA * FindData)
{
    struct dirent * Entry;
    while ((Entry = readdir(Find->dir)) != nullptr)
    {
        if (fnmatch(Find->pattern.c_str(), Entry->d_name, 0) != 0)
        {
            continue;
        }
        memset(FindData, 0, sizeof(*FindData));
        strncpy(FindData->cFileName, Entry->d_name, MAX_PATH - 1);
        FindData->dwFileAttributes = Entry->d_type == DT_DIR ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        return true;
    }
    return false;
}
} // namespace

// Pattern: "<directory>\\<wildcard>" or "<directory>/<wildcard>"
HANDLE FindFirstFileA(const char * Pattern, WIN32_FIND_DATAA * FindData)
{
    std::string Full = Pattern;
    const size_t Slash = Full.find_last_of("\\/");
    const std::string Dir = Slash == std::string::npos ? "." : Full.substr(0, Slash == 0 ? 1 : Slash);
    FindHandle * Find = new FindHandle;
    Find->pattern = Slash == std::string::npos ? Full : Full.substr(Slash + 1);
    Find->dir = opendir(Dir.c_str());
    if (Find->dir == nullptr || !NextEntry(Find, FindData))
    {
        if (Find->dir != nullptr)
        {
            closedir(Find->dir);
        }
        delete Find;
        return INVALID_HANDLE_VALUE;
    }
    return Find;
}

BOOL FindNextFileA(HANDLE Handle, WIN32_FIND_DATAA * FindData)
{
    return NextEntry((FindHandle *)Handle, FindData) ? TRUE : FALSE;
}

BOOL FindClose(HANDLE Handle)
{
    return CloseHandle(Handle);
}

BOOL DeleteFileA(const char * Path)
{
    return unlink(Path) == 0;
}

BOOL CreateDirectoryA(const char * Path, void *)
{
    return mkdir(Path, 0777) == 0;
}

BOOL RemoveDirectoryA(const char * Path)
{
    return rmdir(Path) == 0;
}

// ---- pipes and processes ----

BOOL CreatePipe(HANDLE * ReadPipe, HANDLE * WritePipe, SECURITY_ATTRIBUTES *, DWORD)
{
    int Fds[2];
    if (pipe(Fds) != 0)
    {
        return FALSE;
    }
    // inherited only where CreateProcessA dup2()s them
    fcntl(Fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(Fds[1], F_SETFD, FD_CLOEXEC);
    PipeHandle * Read = new PipeHandle;
    Read->fd = Fds[0];
    PipeHandle * Write = new PipeHandle;
    Write->fd = Fds[1];
    *ReadPipe = Read;
    *WritePipe = Write;
    return TRUE;
}

BOOL ReadFile(HANDLE File, void * Buffer, DWORD NumberOfBytesToRead, DWORD * NumberOfBytesRead, void *)
{
    PipeHandle * Pipe = (PipeHandle *)File;
    ssize_t Count;
    do
    {
        Count = read(Pipe->fd, Buffer, NumberOfBytesToRead);
    } while (Count < 0 && errno == EINTR);
    if (Count < 0)
    {
        return FALSE;
    }
    if (NumberOfBytesRead != nullptr)
    {
        *NumberOfBytesRead = (DWORD)Count;
    }
    return TRUE;
}

BOOL CreateProcessA(const char *, char * CommandLine, void *, void *, BOOL, DWORD, void *, const char * CurrentDirectory, STARTUPINFOA * StartupInfo, PROCESS_INFORMATION * ProcessInformation)
{
    // exec() builds "cmd /c <command>": the shell command is the rest
    std::string Command = CommandLine;
    if (Command.compare(0, 7, "cmd /c ") == 0)
    {
        Command = Command.substr(7);
    }
    int StdIn = -1, StdOut = -1, StdErr = -1;
    if (StartupInfo != nullptr && (StartupInfo->dwFlags & STARTF_USESTDHANDLES))
    {
        StdIn = ((PipeHandle *)StartupInfo->hStdInput)->fd;
        StdOut = ((PipeHandle *)StartupInfo->hStdOutput)->fd;
        StdErr = ((PipeHandle *)StartupInfo->hStdError)->fd;
    }
    // Windows' "start <file>" opens the file with its application
    if (Command.compare(0, 6, "start ") == 0)
    {
        std::string File = Command.substr(6);
        if (File.size() >= 2 && File.front() == '"' && File.back() == '"')
        {
            File = File.substr(1, File.size() - 2);
        }
        std::string Quoted = "'";
        for (char Character : File)
        {
            Quoted += Character == '\'' ? std::string("'\\''") : std::string(1, Character);
        }
        Command = "xdg-open " + Quoted + "'";
    }
    const pid_t Pid = fork();
    if (Pid < 0)
    {
        return FALSE;
    }
    if (Pid == 0)
    {
        // its own process group: ScriptShimTerminateProcesses ends it with what it started; the
        // signal mask of the (Qt) thread that forks is not the child's
        setpgid(0, 0);
        sigset_t NoSignals;
        sigemptyset(&NoSignals);
        sigprocmask(SIG_SETMASK, &NoSignals, nullptr);
        if (StdIn >= 0)
        {
            dup2(StdIn, 0);
            dup2(StdOut, 1);
            dup2(StdErr, 2);
        }
        if (CurrentDirectory != nullptr && chdir(CurrentDirectory) != 0)
        {
            _exit(127);
        }
        execl("/bin/sh", "sh", "-c", Command.c_str(), (char *)nullptr);
        _exit(127);
    }
    setpgid(Pid, Pid); // also here: either side may run first
    ProcessHandle * Process = new ProcessHandle;
    Process->pid = Pid;
    {
        std::lock_guard<std::mutex> Guard(g_ProcessLock);
        g_Processes.insert(Pid);
    }
    ProcessInformation->hProcess = Process;
    ProcessInformation->hThread = nullptr;
    ProcessInformation->dwProcessId = (DWORD)Pid;
    ProcessInformation->dwThreadId = 0;
    return TRUE;
}

BOOL GetExitCodeProcess(HANDLE Handle, DWORD * ExitCode)
{
    ProcessHandle * Process = (ProcessHandle *)Handle;
    if (!Process->exited)
    {
        int Status = 0;
        if (waitpid(Process->pid, &Status, WNOHANG) <= 0)
        {
            *ExitCode = 259; // STILL_ACTIVE
            return TRUE;
        }
        ReapProcess(Process, Status);
    }
    *ExitCode = WIFEXITED(Process->status) ? (DWORD)WEXITSTATUS(Process->status) : (DWORD)(128 + WTERMSIG(Process->status));
    return TRUE;
}

#undef select
#undef FD_SET
#undef FD_ISSET

namespace
{
thread_local bool g_FdSetOverflow = false;
}

void ScriptShimFdSet(int Fd, fd_set * Set)
{
    if (Fd < 0 || Fd >= FD_SETSIZE)
    {
        g_FdSetOverflow = true;
        return;
    }
    __FD_SET(Fd, Set);
}

bool ScriptShimFdIsSet(int Fd, fd_set * Set)
{
    return Fd >= 0 && Fd < FD_SETSIZE && __FD_ISSET(Fd, Set);
}

int ScriptShimSelect(fd_set * ReadFds, fd_set * WriteFds, fd_set * ExceptFds, struct timeval * Timeout)
{
    if (g_FdSetOverflow)
    {
        g_FdSetOverflow = false;
        errno = EBADF;
        return -1;
    }
    int MaxFd = -1;
    for (fd_set * Set : {ReadFds, WriteFds, ExceptFds})
    {
        if (Set == nullptr)
        {
            continue;
        }
        for (int Fd = 0; Fd < FD_SETSIZE; Fd++)
        {
            if (__FD_ISSET(Fd, Set) && Fd > MaxFd)
            {
                MaxFd = Fd;
            }
        }
    }
    struct timeval Remaining;
    if (Timeout != nullptr)
    {
        Remaining = *Timeout;
    }
    return ::select(MaxFd + 1, ReadFds, WriteFds, ExceptFds, Timeout != nullptr ? &Remaining : nullptr);
}
