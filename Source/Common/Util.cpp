#include <cstring>
#include "Util.h"
#include "StdString.h"
#include "path.h"
#ifdef _WIN32
#include <windows.h>

#include <Tlhelp32.h>
#else
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/types.h>
#include <time.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

void pjutil::Sleep(uint32_t timeout)
{
#ifdef _WIN32
    ::Sleep(timeout);
#else
    int was_error;
    struct timespec elapsed, tv;
    elapsed.tv_sec = timeout / 1000;
    elapsed.tv_nsec = (timeout % 1000) * 1000000;
    do
    {
        errno = 0;
        tv.tv_sec = elapsed.tv_sec;
        tv.tv_nsec = elapsed.tv_nsec;
        was_error = nanosleep(&tv, &elapsed);
    } while (was_error && (errno == EINTR));
#endif
}

#ifdef _WIN32
bool pjutil::TerminatedExistingExe()
{
    bool bTerminated = false;
    bool AskedUser = false;
    DWORD pid = GetCurrentProcessId();

    HANDLE nSearch = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (nSearch != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32 lppe;

        memset(&lppe, 0, sizeof(PROCESSENTRY32));
        lppe.dwSize = sizeof(PROCESSENTRY32);
        stdstr ModuleName = CPath(CPath::MODULE_FILE).GetNameExtension();

        if (Process32First(nSearch, &lppe))
        {
            do
            {
                if (_wcsicmp(lppe.szExeFile, ModuleName.ToUTF16().c_str()) != 0 ||
                    lppe.th32ProcessID == pid)
                {
                    continue;
                }
                if (!AskedUser)
                {
                    stdstr_f Message("%s currently running\n\nTerminate PID %d now?", ModuleName.c_str(), lppe.th32ProcessID);
                    stdstr_f Caption("Terminate %s", ModuleName.c_str());

                    AskedUser = true;
                    int res = MessageBox(nullptr, Message.ToUTF16().c_str(), Caption.ToUTF16().c_str(), MB_YESNO | MB_ICONEXCLAMATION);
                    if (res != IDYES)
                    {
                        break;
                    }
                }
                HANDLE hHandle = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, lppe.th32ProcessID);
                if (hHandle != nullptr)
                {
                    if (TerminateProcess(hHandle, 0))
                    {
                        bTerminated = true;
                        WaitForSingleObject(hHandle, 30 * 1000);
                    }
                    else
                    {
                        stdstr_f Message("Failed to terminate PID %d", lppe.th32ProcessID);
                        stdstr_f Caption("Terminate %s failed!", ModuleName.c_str());
                        MessageBox(nullptr, Message.ToUTF16().c_str(), Caption.ToUTF16().c_str(), MB_YESNO | MB_ICONEXCLAMATION);
                    }
                    CloseHandle(hHandle);
                }
            } while (Process32Next(nSearch, &lppe));
        }
        CloseHandle(nSearch);
    }
    return bTerminated;
}
#endif

#ifndef _WIN32
static std::string RunningExeFile()
{
    const char * Dir = getenv("XDG_RUNTIME_DIR");
    return std::string(Dir != nullptr && *Dir != 0 ? Dir : "/tmp") + "/vortex64-" + std::to_string((long)getuid()) + ".pid";
}

static std::string ExePathOf(long pid)
{
    char Buffer[4096];
    ssize_t Len = readlink(("/proc/" + std::to_string(pid) + "/exe").c_str(), Buffer, sizeof(Buffer) - 1);
    return Len > 0 ? std::string(Buffer, (size_t)Len) : std::string();
}

// The pid file is locked (flock) by the running copy for as long as it runs: two copies started at once cannot both
// believe that they are the only one, and a file that a copy left behind is not a running copy.
static int g_PidFileFd = -1;

static bool LockPidFile(bool Wait)
{
    if (g_PidFileFd >= 0)
    {
        return true;
    }
    const int Fd = open(RunningExeFile().c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (Fd < 0)
    {
        return false;
    }
    for (int i = 0; i < (Wait ? 50 : 1); i++)
    {
        if (flock(Fd, LOCK_EX | LOCK_NB) == 0)
        {
            g_PidFileFd = Fd;
            return true;
        }
        if (Wait)
        {
            pjutil::Sleep(100);
        }
    }
    close(Fd);
    return false;
}

int pjutil::ExistingExePid()
{
    if (LockPidFile(false))
    {
        return 0; // nobody else holds it: this copy is the only one (and keeps the lock)
    }
    // another copy has the lock; it writes its process id just after it has it
    long pid = 0;
    for (int i = 0; i < 10 && pid <= 0; i++)
    {
        FILE * File = fopen(RunningExeFile().c_str(), "r");
        if (File != nullptr)
        {
            if (fscanf(File, "%ld", &pid) != 1)
            {
                pid = 0;
            }
            fclose(File);
        }
        if (pid <= 0)
        {
            Sleep(100);
        }
    }
    // only another copy of this executable (a recycled process id is not one)
    if (pid <= 0 || pid == (long)getpid() || kill((pid_t)pid, 0) != 0 || ExePathOf(pid).empty() || ExePathOf(pid) != ExePathOf((long)getpid()))
    {
        return 0;
    }
    return (int)pid;
}

bool pjutil::TerminateExe(int pid)
{
    if (pid <= 0 || kill((pid_t)pid, SIGTERM) != 0)
    {
        return false;
    }
    // the application ends its game and saves its settings on SIGTERM
    for (int i = 0; i < 100 && kill((pid_t)pid, 0) == 0; i++)
    {
        Sleep(100);
    }
    if (kill((pid_t)pid, 0) == 0)
    {
        kill((pid_t)pid, SIGKILL);
        for (int i = 0; i < 20 && kill((pid_t)pid, 0) == 0; i++)
        {
            Sleep(100);
        }
    }
    return kill((pid_t)pid, 0) != 0;
}

void pjutil::RegisterRunningExe()
{
    // the lock is taken (waiting for the copy that was ended to let go of it) and the process id written
    if (!LockPidFile(true))
    {
        return;
    }
    if (ftruncate(g_PidFileFd, 0) == 0)
    {
        const std::string Text = std::to_string((long)getpid()) + "\n";
        if (pwrite(g_PidFileFd, Text.c_str(), Text.size(), 0) < 0)
        {
            // (the file is only informational)
        }
    }
}
#endif
