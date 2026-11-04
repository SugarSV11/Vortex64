#pragma once
#ifndef _WIN32
#include <Common/Thread.h>
#endif
#include <Project64-core/Plugins/PluginBase.h>
#ifndef _WIN32
#include <atomic>
#endif

class CAudioPlugin : public CPlugin
{
public:
    CAudioPlugin(void);
    ~CAudioPlugin();

#ifndef _WIN32
    void Close(RenderWindow * Render);
#endif
    void DacrateChanged(SYSTEM_TYPE Type);
    bool Initiate(CN64System * System, RenderWindow * Window);

    void(CALL * AiLenChanged)(void);
    uint32_t(CALL * AiReadLength)(void);
    void(CALL * ProcessAList)(void);

private:
    CAudioPlugin(const CAudioPlugin &);
    CAudioPlugin & operator=(const CAudioPlugin &);

    virtual int32_t GetDefaultSettingStartRange() const
    {
        return FirstAudioDefaultSet;
    }
    virtual int32_t GetSettingStartRange() const
    {
        return FirstAudioSettings;
    }
    PLUGIN_TYPE type()
    {
        return PLUGIN_TYPE_AUDIO;
    }

#ifdef _WIN32
    void * m_hAudioThread;
#else
    CThread * m_AudioThread;
    std::atomic<bool> m_StopAudioThread;
    void StopAudioThread(void);
#endif

    bool LoadFunctions(void);
    void UnloadPluginDetails(void);

    void(CALL * AiUpdate)(int32_t Wait);
    void(CALL * AiDacrateChanged)(SYSTEM_TYPE Type);

#ifdef _WIN32
    static void AudioThread(CAudioPlugin * _this);
#else
    static void * AudioThread(void * param);
#endif
};
