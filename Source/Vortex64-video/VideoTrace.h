#pragma once
// Diagnostics of the video plugin: with VORTEX_VIDEO_TRACE=1 in the environment the plugin prints to stderr what it
// decides (the memory it sees, the microcode it found, whether the display lists draw anything). A game that goes on
// with a black picture shows display lists with no triangles, or no display lists at all.
#include <cstdio>
#include <cstdlib>
inline bool VideoTraceOn()
{
    static int On = -1;
    if (On < 0)
    {
        On = getenv("VORTEX_VIDEO_TRACE") != nullptr ? 1 : 0;
    }
    return On == 1;
}
#define VTRACE(...)                                   \
    do                                                \
    {                                                 \
        if (VideoTraceOn())                           \
        {                                             \
            fprintf(stderr, "[video] " __VA_ARGS__);  \
            fputc('\n', stderr);                      \
        }                                             \
    } while (0)
