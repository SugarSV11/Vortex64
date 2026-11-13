#include <stdio.h>
#include <stdlib.h>

#include "RSP Command.h"
#include "Rsp.h"

// The RSP command debugger window is a set of Win32 dialogs and has not
// been ported to other platforms. On non-Windows builds we only provide
// portable no-op stand-ins (see the #else branch below) for the
// externs/functions declared in "RSP Command.h" so the rest of the
// plugin (RSPDebuggerUI.cpp, Main.cpp) still links and runs correctly
// with this debugger window effectively disabled.

bool InRSPCommandsWindow = false;
bool Stepping_Commands = false, WaitingForStep = false;

void DumpRSPCode(void)
{
}

void DumpRSPData(void)
{
}

void Disable_RSP_Commands_Window(void)
{
}

void Enable_RSP_Commands_Window(void)
{
}

void Enter_RSP_Commands_Window(void)
{
}

void RefreshRSPCommands(void)
{
}

void SetRSPCommandToRunning(void)
{
    Stepping_Commands = false;
}

void SetRSPCommandToStepping(void)
{
    Stepping_Commands = true;
}

void SetRSPCommandViewto(uint32_t /*NewLocation*/)
{
}

