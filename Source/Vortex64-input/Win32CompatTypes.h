// Project64 - A Nintendo 64 emulator
// https://www.pj64-emu.com/
// Copyright(C) 2001-2021 Project64.
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html

// Minimal portable stand-ins for the Win32 types/macros used by the input
// plugin's device-scanning code (button/GUID plumbing, the (now-inert on
// Linux) event-pump window proc). Only what is actually referenced is
// declared; real Win32 headers are used unchanged on Windows.
#pragma once

#ifdef _WIN32
#include <Windows.h>
#include <guiddef.h>
#else
#include <cstdint>
#include <cstring>

typedef void * HINSTANCE;
typedef void * HWND;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef intptr_t LONG_PTR;
typedef uintptr_t UINT_PTR;
typedef uintptr_t DWORD_PTR;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef LONG * LPLONG;
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef unsigned int UINT;

#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef WINAPI
#define WINAPI
#endif
#ifndef WM_USER
#define WM_USER 0x0400
#endif
#ifndef LOWORD
#define LOWORD(l) ((WORD)((DWORD_PTR)(l) & 0xffff))
#endif
#ifndef HIWORD
#define HIWORD(l) ((WORD)(((DWORD_PTR)(l) >> 16) & 0xffff))
#endif

// Opaque 16-byte device identifier, matching the field layout (and names)
// that InputSettings.cpp's sscanf/GUIDtoString rely on.
struct GUID
{
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
};

inline bool operator==(const GUID & a, const GUID & b)
{
    return memcmp(&a, &b, sizeof(GUID)) == 0;
}

inline bool operator!=(const GUID & a, const GUID & b)
{
    return !(a == b);
}

#endif // _WIN32
