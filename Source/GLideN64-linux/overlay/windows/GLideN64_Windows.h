// Linux stand-in for the "GLideN64_Windows.h" that the windows/ sources of GLideN64 include
// (the file in the submodule is spelled GLideN64_windows.h). Compiled with OS_LINUX, Platform.h
// then pulls in winlnxdefs.h, whose HWND is an int: an X11 window id fits in it.
#pragma once
#include "../../GLideN64/Source/src/windows/GLideN64_windows.h"
