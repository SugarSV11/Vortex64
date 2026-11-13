#include "stdafx.h"

#include "PluginList.h"
#include <Vortex64-core/Plugins/PluginBase.h>
#ifdef _WIN32
#include <io.h>
#else
// Linux: plugins are shared objects loaded with dlopen()/dlsym() (through
// Common/DynamicLibrary), everything else in this file is unchanged.
#include <Common/DynamicLibrary.h>
#include <string.h>
#define PLUGIN_FILE_MASK "*.so"
typedef DynLibHandle HMODULE;
typedef const char * LPCSTR;
#define LoadLibrary(Name) DynamicLibraryOpen(Name, false)
#define GetProcAddress(Lib, Name) DynamicLibraryGetProc(Lib, Name)
#define FreeLibrary(Lib) DynamicLibraryClose(Lib)
#endif
#ifndef PLUGIN_FILE_MASK
#define PLUGIN_FILE_MASK "*.dll"
#endif

CPluginList::CPluginList(bool bAutoFill /* = true */) :
    m_PluginDir(g_Settings->LoadStringVal(Directory_Plugin), "")
{
    m_PluginDir.NormalizePath(CPath(CPath::MODULE_DIRECTORY));
    if (bAutoFill)
    {
        LoadList();
    }
}

CPluginList::~CPluginList()
{
}

int CPluginList::GetPluginCount() const
{
    return (int)m_PluginList.size();
}

const CPluginList::PLUGIN * CPluginList::GetPluginInfo(int indx) const
{
    if (indx < 0 || indx >= (int)m_PluginList.size())
    {
        return nullptr;
    }
    return &m_PluginList[indx];
}

bool CPluginList::LoadList()
{
    WriteTrace(TraceUserInterface, TraceDebug, "Start");
    m_PluginList.clear();
    AddPluginFromDir(m_PluginDir);
    WriteTrace(TraceUserInterface, TraceDebug, "Done");
    return true;
}

void CPluginList::AddPluginFromDir(CPath Dir)
{
    Dir.SetNameExtension("*");
    if (Dir.FindFirst(CPath::FIND_ATTRIBUTE_SUBDIR))
    {
        do
        {
            AddPluginFromDir(Dir);
        } while (Dir.FindNext());
        Dir.UpDirectory();
    }

    Dir.SetNameExtension(PLUGIN_FILE_MASK);
    if (Dir.FindFirst())
    {
        HMODULE hLib = nullptr;
        do
        {
            if (hLib)
            {
                FreeLibrary(hLib);
                hLib = nullptr;
            }

            //UINT LastErrorMode = SetErrorMode( SEM_FAILCRITICALERRORS );
            WriteTrace(TraceUserInterface, TraceDebug, "Loading %s", (LPCSTR)Dir);
#ifdef _WIN32
            hLib = LoadLibrary(stdstr((LPCSTR)Dir).ToUTF16().c_str());
#else
            hLib = LoadLibrary((const char *)Dir);
#endif
            //SetErrorMode(LastErrorMode);

            if (hLib == nullptr)
            {
#ifdef _WIN32
                DWORD LoadError = GetLastError();
                WriteTrace(TraceUserInterface, TraceDebug, "Failed to load %s (error: %d)", (LPCSTR)Dir, LoadError);
#else
                WriteTrace(TraceUserInterface, TraceDebug, "Failed to load %s", (const char *)Dir);
#endif
                continue;
            }

            void(CALL * GetDllInfo)(PLUGIN_INFO * PluginInfo);
            GetDllInfo = (void(CALL *)(PLUGIN_INFO *))GetProcAddress(hLib, "GetDllInfo");
            if (GetDllInfo == nullptr)
            {
                continue;
            }

            PLUGIN Plugin = {0};
            Plugin.Info.Reserved2 = true;
            GetDllInfo(&Plugin.Info);
            if (!CPlugin::ValidPluginVersion(Plugin.Info))
            {
                continue;
            }

            Plugin.FullPath = Dir;
            Plugin.FileName = stdstr((const char *)Dir).substr(strlen(m_PluginDir));

            if (GetProcAddress(hLib, "DllAbout") != nullptr)
            {
                Plugin.AboutFunction = true;
            }
            m_PluginList.push_back(Plugin);
        } while (Dir.FindNext());

        if (hLib)
        {
            FreeLibrary(hLib);
            hLib = nullptr;
        }
    }
}
