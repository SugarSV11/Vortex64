#pragma once
#include <Common/Thread.h>
#include <Vortex64-core/Plugins/PluginBase.h>
#include <atomic>

class CAudioPlugin : public CPlugin
{
public:
    CAudioPlugin(void);
    ~CAudioPlugin();

    void Close(RenderWindow * Render);
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

    CThread * m_AudioThread;
    std::atomic<bool> m_StopAudioThread;
    void StopAudioThread(void);

    bool LoadFunctions(void);
    void UnloadPluginDetails(void);

    void(CALL * AiUpdate)(int32_t Wait);
    void(CALL * AiDacrateChanged)(SYSTEM_TYPE Type);

    static void * AudioThread(void * param);
};
