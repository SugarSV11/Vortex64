#pragma once

#ifndef _WIN32
#include <alloca.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <stdarg.h>
#include <strings.h>

#define stricmp strcasecmp
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _snprintf snprintf
#define _isnan isnan

// __debugbreak() is an MSVC intrinsic that traps into an attached debugger;
// raise(SIGTRAP) is the POSIX equivalent (gdb stops on it just the same).
#define __debugbreak() raise(SIGTRAP)

// _aligned_malloc/_aligned_free are MSVC CRT calls; POSIX's aligned_alloc()
// covers the allocation side (size must be a multiple of alignment, which
// every _aligned_malloc(size, 16) call in this codebase already satisfies
// since 16 divides the recompiler's allocation sizes), and plain free()
// works for anything aligned_alloc() returned.
#define _aligned_malloc(size, alignment) aligned_alloc((alignment), (((size) + (alignment)-1) / (alignment)) * (alignment))
#define _aligned_free(ptr) free(ptr)

// _Printf_format_string_ is an MSVC/SAL2 (<sal.h>) parameter annotation that
// enables compile-time printf-format checking; it has no GCC/Clang
// equivalent placed the same way (that's normally done with a
// __attribute__((format(printf,...))) after the function instead), so just
// drop the annotation rather than the diagnostic it was providing.
#define _Printf_format_string_

int _vscprintf(const char * format, va_list pargs);

// the Windows CRT names that the 7z ROM listing and loading (RomList.cpp, N64Rom.cpp) use
#define _MAX_PATH 260
#define _MAX_DRIVE 3
#define _MAX_DIR 256
#define _MAX_FNAME 256
#define _MAX_EXT 256
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
typedef const char * LPCSTR;
typedef unsigned long ULONG;
void _splitpath(const char * path, char * drive, char * dir, char * fname, char * ext);

#endif

// 7z archives as ROMs: upstream enables them for Windows only (its other target, Android, has no
// C7zip), but the decoder (3rdParty/7zip) builds on Linux too
#if defined(_WIN32) || defined(__linux__)
#define PJ64_HAVE_7ZIP
#endif
