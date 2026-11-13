// Project64 - A Nintendo 64 emulator
// https://www.pj64-emu.com/
// Copyright(C) 2001-2021 Project64.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html

// On Windows, Win32KeyboardScan.cpp ORs in a handful of extra keys
// (media/browser keys, NumLock, etc.) that GetAsyncKeyState can see but
// that historically weren't reliably reported through SDL's own scancode
// table. SDL_GetKeyboardState() on Linux already reports the full scancode
// set the caller merges into, so there is nothing extra to add here.
#include "Win32KeyboardScan.h"

void Win32MergeKeyboardOrInto(uint8_t * /*keyboardState*/, size_t /*byteCount*/)
{
}
