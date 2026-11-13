#pragma once
#include <stdint.h>

class pjutil
{
public:
    static void Sleep(uint32_t timeout);
    static bool TerminatedExistingExe();
    // Linux counterpart of TerminatedExistingExe: the process id of another running copy of this executable
    // (from the file in the runtime directory), 0 if there is none; the caller asks and terminates it
    static int ExistingExePid();
    static bool TerminateExe(int pid);
    // records this process as the running copy
    static void RegisterRunningExe();

private:
    pjutil(void);
    pjutil(const pjutil &);
    pjutil & operator=(const pjutil &);
};
