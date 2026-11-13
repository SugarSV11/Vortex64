#pragma once

// Linux counterpart of Source/Vortex64-ui/stdafx.h for the upstream files that
// the Qt frontend compiles unchanged (Project64/Settings/UISettings.cpp, ...).
// The Windows header pulls in <windows.h>, WTL and friends; only the core
// pieces those files actually use are included here.
#include <stdint.h>
#include <Vortex64-core/N64System/N64System.h>
#include <Common/path.h>
#include <Common/StdString.h>
#include <Common/Trace.h>
#include <Vortex64-plugin-spec/Base.h>
#include <Vortex64-core/N64System/SystemGlobals.h>
#include <Vortex64-core/Settings.h>
#include <Vortex64-core/Settings/SettingsID.h>

// Types and constants that the upstream UI files compiled here unchanged
// (Project64/UserInterface/MenuShortCuts.cpp) take from <windows.h>.
#include <Common/CriticalSection.h>
#include <Vortex64-core/Notification.h>
#include <Vortex64-core/Multilanguage.h>
#include <Vortex64-ui/Settings/UISettings.h>
#include <list>
#include <map>
#include <string>
#include "MainMenuID.h"
typedef uint16_t WORD;
typedef const wchar_t * LPCTSTR;
#ifndef TRUE
#define TRUE 1
#endif
#define VK_RETURN 0x0D
#define VK_PAUSE 0x13
#define VK_ESCAPE 0x1B
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F7 0x76
#define VK_F9 0x78
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_OEM_PLUS 0xBB
#define VK_OEM_MINUS 0xBD
#include <Vortex64-ui/UserInterface/MenuShortCuts.h>

// Names of <windows.h>/MSVC and of the upstream stdafx (using namespace std) that the
// debugger back end (Project64/UserInterface/Debugger) compiled unchanged relies on.
#include <set>
#include <vector>
#include <string.h>
using std::set;
using std::vector;
typedef intptr_t INT_PTR;
typedef uintptr_t UINT_PTR;
#define _strdup strdup
#define strtok_s strtok_r
#include <algorithm>
using std::sort;
#define _byteswap_ushort __builtin_bswap16
#define _byteswap_ulong __builtin_bswap32
#define _byteswap_uint64 __builtin_bswap64
#define MB_OK 0x0
#define MB_ICONWARNING 0x30
// Implemented in Debugger/DebugCommon.cpp with a Qt message box (blocks the caller;
// safe from the emulation thread)
int MessageBox(void * Parent, const wchar_t * Text, const wchar_t * Caption, unsigned Type);

#include "Debugger/DebuggerUI.h"
