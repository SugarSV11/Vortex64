#pragma once
// These tests inject fake keyboard, mouse or gamepad events (XTEST / uinput). Run against the X server of a
// person's desktop they would move their pointer and type into whatever window has the focus. They therefore
// refuse to run unless the caller says the X server is a private one (Scripts/linux/run-gui-tests.sh starts
// an Xvfb and sets VORTEX_ISOLATED_X=1).
#include <cstdio>
#include <cstdlib>

inline void RequireIsolatedX()
{
    const char * Flag = getenv("VORTEX_ISOLATED_X");
    if (Flag != nullptr && Flag[0] == '1')
    {
        return;
    }
    printf("SKIP this test injects fake input events; run it through Scripts/linux/run-gui-tests.sh (private Xvfb) "
           "or set VORTEX_ISOLATED_X=1 if DISPLAY is a throwaway X server\n");
    exit(0);
}
