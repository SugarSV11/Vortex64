// The RSP register debugger window is a set of Win32 dialogs and has not
// been ported to other platforms. On non-Windows builds we only provide
// portable no-op stand-ins (see the #else branch below) for the
// functions declared in "RSPRegistersUI.h" so the rest of the plugin
// still links and runs correctly with this debugger window effectively
// disabled.

void UpdateRSPRegistersScreen(void)
{
}

void Enter_RSP_Register_Window(void)
{
}

