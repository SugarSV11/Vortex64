// Project64 - A Nintendo 64 emulator
// http://www.pj64-emu.com/
// Copyright(C) 2001-2021 Project64
// Copyright(C) 2003-2009 Sergey 'Gonetz' Lipski
// Copyright(C) 2002 Dave2001
// GNU/GPLv2 licensed: https://gnu.org/licenses/gpl-2.0.html
#include "Gfx_1.3.h"
#include <dlfcn.h>
#include <string>
#include "DepthBufferRender.h"
#include "Config.h"
#include "trace.h"
#include "ScreenResolution.h"
#include <Common/StdString.h>
#include <Settings/Settings.h>
#include "SettingsID.h"
#include "ScreenResolution.h"


void CloseConfig();


/******************************************************************
Function: DllConfig
Purpose:  This function is optional function that is provided
to allow the user to configure the dll
input:    a handle to the window that calls this function
output:   none
*******************************************************************/
void CALL DllConfig(void * hParent)
{
    WriteTrace(TraceGlide64, TraceDebug, "-");
    // Linux: the property sheet is implemented with Qt6 in ConfigQt.cpp, built
    // as libVortex64-video-qtconfig.so next to this plugin and loaded on
    // demand so that this plugin has no Qt dependency (the console frontend
    // has no Qt). The renderer is re-created after the dialog by the emulation
    // thread (it owns the GL context): see RequestConfigReinit() in Main.cpp.
    if (!g_romopen)
    {
        char name[21] = "DEFAULT";
        g_settings->ReadGameSettings(name);
        ZLUT_init();
    }

    Dl_info Info;
    if (dladdr(reinterpret_cast<void *>(&DllConfig), &Info) == 0 || Info.dli_fname == nullptr)
    {
        return;
    }
    std::string ModulePath(Info.dli_fname);
    size_t Slash = ModulePath.rfind('/');
    ModulePath = (Slash == std::string::npos ? std::string() : ModulePath.substr(0, Slash + 1)) + "libVortex64-video-qtconfig.so";
    void * Module = dlopen(ModulePath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (Module == nullptr)
    {
        WriteTrace(TraceGlide64, TraceWarning, "Unable to load %s: %s", ModulePath.c_str(), dlerror());
        return;
    }
    void (*QtConfig)(void *) = reinterpret_cast<void (*)(void *)>(dlsym(Module, "PJ64VideoQtConfig"));
    if (QtConfig != nullptr)
    {
        QtConfig(hParent);
    }
    dlclose(Module);
    if (g_romopen)
    {
        RequestConfigReinit(); // (the renderer is made again by the emulation thread, as upstream does around the dialog)
    }
}

void CloseConfig()
{
    if (g_romopen)
    {
        if (g_settings->fb_depth_render_enabled())
        {
            ZLUT_init();
        }
        InitGfx();
    }
}


/******************************************************************
Function: DllAbout
Purpose:  This function is optional function that is provided
to give further information about the DLL.
input:    a handle to the window that calls this function
output:   none
*******************************************************************/
void CALL DllAbout(void * /*hParent*/)
{
}