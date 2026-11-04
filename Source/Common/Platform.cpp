#include "Platform.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#ifndef _WIN32
#include <string.h>

// _splitpath of the Windows CRT: there is no drive here; dir keeps its last separator
void _splitpath(const char * path, char * drive, char * dir, char * fname, char * ext)
{
    if (drive != nullptr)
    {
        drive[0] = '\0';
    }
    const char * Base = strrchr(path, '/');
    Base = Base != nullptr ? Base + 1 : path;
    if (dir != nullptr)
    {
        const size_t Length = Base - path;
        memcpy(dir, path, Length);
        dir[Length] = '\0';
    }
    const char * Dot = strrchr(Base, '.');
    const size_t NameLength = Dot != nullptr ? (size_t)(Dot - Base) : strlen(Base);
    if (fname != nullptr)
    {
        memcpy(fname, Base, NameLength);
        fname[NameLength] = '\0';
    }
    if (ext != nullptr)
    {
        strcpy(ext, Dot != nullptr ? Dot : "");
    }
}

int _vscprintf(const char * format, va_list pargs)
{
    int retval;
    va_list argcopy;
    va_copy(argcopy, pargs);
    retval = vsnprintf(nullptr, 0, format, argcopy);
    va_end(argcopy);
    return retval;
}
#endif