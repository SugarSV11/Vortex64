#pragma once
#include <Common/path.h>
#include <Project64-core/Settings.h>
#include <Project64-core/Settings/DebugSettings.h>
#include <Project64-plugin-spec/Base.h>
#include <Project64-plugin-spec/PresentControl.h>
#include <list>
#include <mutex>
#include <string>

// Enums
enum SETTING_DATA_TYPE
{
    Data_DWORD_General = 0,      // A uint32_t setting used anywhere
    Data_String_General = 1,     // A string setting used anywhere
    Data_DWORD_Game = 2,         // A uint32_t associated with the current game
    Data_String_Game = 3,        // A string associated with the current game
    Data_DWORD_RDB = 4,          // A uint32_t associated with the current game in the ROM database
    Data_String_RDB = 5,         // A string associated with the current game in the ROM database
    Data_DWORD_RDB_Setting = 6,  // A uint32_t read from the ROM database, with config file
    Data_String_RDB_Setting = 7, // A string read from the ROM database, with config file
};

typedef struct
{
    uint32_t dwSize;
    int32_t DefaultStartRange;
    int32_t SettingStartRange;
    int32_t MaximumSettings;
    int32_t NoDefault;
    int32_t DefaultLocation;
    void * handle;
    uint32_t (*GetSetting)(void * handle, int32_t ID);
    const char * (*GetSettingSz)(void * handle, int32_t ID, char * Buffer, int32_t BufferLen);
    void (*SetSetting)(void * handle, int32_t ID, uint32_t Value);
    void (*SetSettingSz)(void * handle, int32_t ID, const char * Value);
    void (*RegisterSetting)(void * handle, int32_t ID, int32_t DefaultID, SettingDataType Type,
                            SettingType Location, const char * Category, const char * DefaultStr, uint32_t Value);
    void (*UseUnregisteredSetting)(int32_t ID);
} PLUGIN_SETTINGS;

typedef struct
{
    uint32_t (*FindSystemSettingId)(void * handle, const char * Name);
} PLUGIN_SETTINGS2;

typedef struct
{
    void (*FlushSettings)(void * handle);
} PLUGIN_SETTINGS3;

typedef struct _PLUGIN_SETTINGS_NOTIFICATION
{
    typedef void (*SettingChangedFunc)(void *);

    void (*RegisterChangeCB)(void * handle, int ID, void * Data, SettingChangedFunc Func);
    void (*UnregisterChangeCB)(void * handle, int ID, void * Data, SettingChangedFunc Func);
} PLUGIN_SETTINGS_NOTIFICATION;

typedef struct
{
    void (*DisplayError)(const char * Message);
    void (*FatalError)(const char * Message);
    void (*DisplayMessage)(int DisplayTime, const char * Message);
    void (*DisplayMessage2)(const char * Message);
    void (*BreakPoint)(const char * FileName, int32_t LineNumber);
} PLUGIN_NOTIFICATION;

class CSettings;
class CGfxPlugin;
class CAudioPlugin;
class CRSP_Plugin;
class CControl_Plugin;
class CN64System;
class CPlugins;

#if defined(_WIN32)
#include <objbase.h>
#else
#define __interface struct
#endif

__interface RenderWindow
{
#ifdef _WIN32
    virtual bool ResetPluginsInUiThread(CPlugins * plugins, CN64System * System) = 0;
    virtual void * GetWindowHandle(void) const = 0;
    virtual void * GetStatusBar(void) const = 0;
    virtual void * GetModuleInstance(void) const = 0;
#elif defined(ANDROID)
    virtual void GfxThreadInit() = 0;
    virtual void GfxThreadDone() = 0;
    virtual void SwapWindow() = 0;
#else
    // Desktop Linux: lets a frontend (Project64-gui) hand the video plugin
    // a real native window to render into (see GFXPlugin.cpp's Info.hWnd),
    // so gfxSstWinOpen() (Project64-video/Renderer/OGLglitchmain.cpp) can
    // use SDL_CreateWindowFrom() to embed instead of opening its own
    // top-level window. A frontend with no window of its own (Project64-cli)
    // just returns nullptr here, which keeps that plugin's existing
    // standalone-SDL2-window behavior unchanged.
    virtual void * GetWindowHandle(void) const = 0;
#endif
};

class CPlugins
{
public:
    // Functions
    CPlugins(SettingID PluginDirSetting, bool SyncPlugins);
    ~CPlugins();

    bool Initiate(CN64System * System);
    void RomOpened(void);
    void RomClosed(void);
    void SetRenderWindows(RenderWindow * MainWindow, RenderWindow * SyncWindow);
    void ConfigPlugin(void * hParent, PLUGIN_TYPE Type);
    bool CopyPlugins(const stdstr & DstDir) const;
    void CreatePlugins(void);
    bool Reset(CN64System * System);
    bool ResetInUiThread(CN64System * System);
    void GameReset(void);

    // How the presentation is synchronized with the monitor (Project64-plugin-spec/PresentControl.h). The hint is kept here and goes to the
    // video plugin that is loaded now and to every one loaded later; the plugin cannot be deleted while it is called (the frontend calls
    // from its own thread, the emulation thread replaces plugins). Returns false when no plugin takes it (yet).
    bool SetPresentHint(const PJ64_PRESENT_HINT & Hint);
    bool GetPresentStatus(PJ64_PRESENT_STATUS & Status, std::string * PluginName = nullptr);

    inline CGfxPlugin * Gfx(void) const
    {
        return m_Gfx;
    }
    inline CAudioPlugin * Audio(void) const
    {
        return m_Audio;
    }
    inline CRSP_Plugin * RSP(void) const
    {
        return m_RSP;
    }
    inline CControl_Plugin * Control(void) const
    {
        return m_Control;
    }

    inline RenderWindow * MainWindow(void) const
    {
        return m_MainWindow;
    }
    inline RenderWindow * SyncWindow(void) const
    {
        return m_SyncWindow;
    }

    inline bool initilized(void) const
    {
        return m_initilized;
    }

private:
    CPlugins(void);
    CPlugins(const CPlugins &);
    CPlugins & operator=(const CPlugins &);

    void DestroyGfxPlugin(void);
    void DestroyAudioPlugin(void);
    void DestroyRspPlugin(void);
    void DestroyControlPlugin(void);

    static void PluginChanged(CPlugins * _this);

    RenderWindow * m_MainWindow;
    RenderWindow * m_SyncWindow;

    SettingID m_PluginDirSetting;
    CPath m_PluginDir;
    bool m_ReloadAll; // the plugin directory changed: every plugin is loaded again (also when the file names are the same)

    // Plugins
    CGfxPlugin * m_Gfx;
    std::recursive_mutex m_PresentLock; // the video plugin is not called while it is deleted
    PJ64_PRESENT_HINT m_PresentHint = {};
    bool m_HasPresentHint = false;
    CAudioPlugin * m_Audio;
    CRSP_Plugin * m_RSP;
    CControl_Plugin * m_Control;

    stdstr m_GfxFile;
    stdstr m_AudioFile;
    stdstr m_RSPFile;
    stdstr m_ControlFile;
    bool m_initilized;
    bool m_SyncPlugins;
};

// Dummy Functions
void DummyCheckInterrupts(void);
void DummyFunction(void);
