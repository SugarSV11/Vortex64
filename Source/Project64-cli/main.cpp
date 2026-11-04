// Vortex64 command line — minimal CLI frontend (on the Project64 core).
//
// This is the smallest thing that can drive the core end to end without a
// GUI: implement CNotification (LinuxNotification.*) and the non-Windows
// RenderWindow interface (both are no-ops on the desktop Linux path — see
// Project64-core/Plugins/PluginBase.cpp: GfxThreadInit/GfxThreadDone/
// SwapWindow are only ever called under #ifdef ANDROID), point the plugin
// system at the .so files built alongside this binary, and call
// CN64System::RunFileImage(). Enough to validate that core + video plugin
// + ROM loading work together as real, native Linux code before any GUI
// work starts.
#include <Project64-core/AppInit.h>
#include <csignal>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/Settings.h>
#include <Common/path.h>

#include "LinuxNotification.h"

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

class CLinuxRenderWindow : public RenderWindow
{
public:
    void * GetWindowHandle(void) const override
    {
        // No window of our own to embed the video plugin into - it opens
        // its own standalone SDL2 window instead (see gfxSstWinOpen()'s
        // desktop Linux branch), same as before RenderWindow gained this
        // method for Project64-gui's benefit.
        return nullptr;
    }
};

static volatile sig_atomic_t g_QuitRequested = 0;

static void OnQuitSignal(int)
{
    g_QuitRequested = 1;
}

int main(int argc, char ** argv)
{
    signal(SIGINT, OnQuitSignal);
    signal(SIGTERM, OnQuitSignal);
    // The video plugin (Project64-video/Renderer/OGLglitchmain.cpp) needs a
    // real X11 window to create its SDL2 GL context under (see
    // gfxSstWinOpen() and GetWindowHandle() above) — SDL2's Wayland driver
    // is a separate, still-immature backend this port hasn't been
    // validated against. Every test of this port so far has relied on the
    // launching shell remembering to export SDL_VIDEODRIVER=x11 (see
    // LINUX_PORT.md); forcing it here instead makes that a guarantee of
    // the binary itself rather than an easy-to-forget invocation detail.
    // setenv()'s overwrite=0 leaves an explicit caller override (e.g. to
    // test the Wayland driver deliberately) intact.
    setenv("SDL_VIDEODRIVER", "x11", 0);

    printf("========================================\n");
    printf(" Vortex64 (command line)\n");
    printf("========================================\n");

    if (argc < 2)
    {
        printf("Usage: %s <ROM> [--plugin-dir DIR] [--recompiler|--interpreter]\n", argv[0]);
        return 0;
    }

    // AppInit()'s BaseDirectory ends up wrapped in a plain std::string
    // (CSettingTypeTempString), and std::string(nullptr) is undefined
    // behavior (throws in libstdc++) — so this can't just be nullptr the
    // way a Windows caller might pass "". Resolve the executable's own
    // directory via CPath::MODULE_DIRECTORY (works before AppInit/
    // g_Settings exist; see Common/path.cpp) and pass that instead.
    std::string BaseDirectory = CPath(CPath::MODULE_DIRECTORY).GetDirectory();

    static CLinuxNotification Notify;
    // The CLI parses its own options below; AppInit()'s ParseCommand() only
    // accepts a ROM as the very last argument and rejects everything else.
    char * AppInitArgv[] = {argv[0], nullptr};
    if (!AppInit(&Notify, BaseDirectory.c_str(), 1, AppInitArgv))
    {
        fprintf(stderr, "[Vortex64] AppInit() failed.\n");
        return 1;
    }
    printf("[Vortex64] AppInit() OK.\n");

    // Point the plugin loader at the directory this binary was built into
    // (where CMake places libProject64-video.so etc. next to it) unless
    // told otherwise. CPath::MODULE_DIRECTORY now resolves via
    // /proc/self/exe (see Common/path.cpp), so this works regardless of
    // the caller's current working directory.
    for (int i = 2; i + 1 < argc; i++)
    {
        if (stdstr(argv[i]) == "--plugin-dir")
        {
            g_Settings->SaveString(Directory_Plugin, CPath(argv[i + 1], "").GetDirectory());
        }
    }

    // Both CPU cores are available, as in upstream. The recompiler is the default on Linux
    // (Settings.cpp: Setting_ForceInterpreterCPU); --recompiler / --interpreter override the
    // "Force Interpreter CPU" setting for this run. BreakPoint() on this frontend stops emulation
    // cleanly (see LinuxNotification.cpp).
    for (int i = 2; i < argc; i++)
    {
        if (stdstr(argv[i]) == "--recompiler")
        {
            g_Settings->SaveBool(Setting_ForceInterpreterCPU, false);
        }
        else if (stdstr(argv[i]) == "--interpreter")
        {
            g_Settings->SaveBool(Setting_ForceInterpreterCPU, true);
        }
    }

    // g_Plugins already exists here: AppInit() (Project64-core/AppInit.cpp)
    // unconditionally creates it as part of its own standard startup
    // sequence. Reuse it rather than leaking that one by overwriting the
    // pointer with a second new CPlugins - SetRenderWindows() is a plain
    // pointer assignment (safe on a freshly-constructed CPlugins) and
    // CreatePlugins() short-circuits per plugin if already loaded (see
    // LoadPlugin<> in Project64-core/Plugins/Plugin.cpp).
    static CLinuxRenderWindow MainWindow;
    g_Plugins->SetRenderWindows(&MainWindow, &MainWindow);
    g_Plugins->CreatePlugins();

    if (g_Plugins->Gfx() == nullptr)
    {
        fprintf(stderr, "[Vortex64] Video plugin failed to load from %s\n", g_Settings->LoadStringVal(Directory_Plugin).c_str());
    }
    else
    {
        printf("[Vortex64] Video plugin loaded.\n");
    }

    const char * RomPath = argv[1];
    printf("\n============================================================\n");
    printf(" LOADING ROM: %s\n", RomPath);
    printf("============================================================\n");

    bool Result = CN64System::RunFileImage(RomPath);
    printf(Result ? "\n[Vortex64] ROM loaded and running.\n" : "\n[Vortex64] Failed to load ROM.\n");

    // RunFileImage() always returns true once the ROM file itself loads,
    // even if plugin/CPU init inside RunLoadedImage() then failed (see
    // CN64System's constructor: it just logs and returns early on failure,
    // and StartEmulation2() only prints MSG_PLUGIN_NOT_INIT without ever
    // calling CloseSystem()). g_BaseSystem is still a valid non-null
    // pointer in that case, so without this check the wait loop below
    // would spin on usleep() forever with nothing actually running.
    if (Result && !g_Plugins->initilized())
    {
        fprintf(stderr, "[Vortex64] Emulation failed to initialize (plugins not ready); not entering run loop.\n");
        Result = false;
    }

    if (Result)
    {
        // Pump until the game window is closed. RunFileImage() above
        // already started the emulation thread when Setting_AutoStart is
        // on; ProcessGuiMessages() is where a GUI frontend would run its
        // own event loop, but ours has none, so just idle here.
        // The loop ends when emulation stops by itself (game over, BreakPoint,
        // End Emulation) or when SIGINT/SIGTERM asks for a clean shutdown.
        bool SawRunning = false;
        while (g_BaseSystem != nullptr && !g_QuitRequested)
        {
            const bool Running = g_Settings->LoadBool(GameRunning_CPU_Running);
            if (Running)
            {
                SawRunning = true;
            }
            else if (SawRunning)
            {
                break;
            }
            usleep(16 * 1000);
        }
        if (g_BaseSystem != nullptr)
        {
            CN64System::CloseSystem();
        }
    }

    printf("\n[Vortex64] Shutting down...\n");
    delete g_Plugins;
    g_Plugins = nullptr;
    AppCleanup();
    printf("[Vortex64] AppCleanup() OK.\n");

    return Result ? 0 : 1;
}
