#pragma once
#include <Vortex64-core/Plugins/PluginBase.h>
#include <Vortex64-plugin-spec/PresentControl.h>

class CGfxPlugin : public CPlugin
{
public:
    CGfxPlugin(void);
    ~CGfxPlugin();

    bool LoadFunctions(void);
    bool Initiate(CN64System * System, RenderWindow * Window);

    void(CALL * CaptureScreen)(const char *);
    void(CALL * ChangeWindow)(void);
    void(CALL * DrawScreen)(void);
    void(CALL * DrawStatus)(const char * lpString, int32_t RightAlign);
    void(CALL * MoveScreen)(int32_t xpos, int32_t ypos);
    void(CALL * ProcessDList)(void);
    void(CALL * ProcessRDPList)(void);
    void(CALL * ShowCFB)(void);
    void(CALL * UpdateScreen)(void);
    void(CALL * ViStatusChanged)(void);
    void(CALL * ViWidthChanged)(void);
    void(CALL * SoftReset)(void);

    // How the presentation is synchronized with the monitor (optional: a plugin without them keeps its own setting)
    void(CALL * PJ64SetPresentHint)(const PJ64_PRESENT_HINT * Hint);
    void(CALL * PJ64GetPresentStatus)(PJ64_PRESENT_STATUS * Status);

    // ROM browser
    void *(CALL * GetRomBrowserMenu)(void); // Items should have an ID between 4101 and 4200
    void(CALL * OnRomBrowserMenuItem)(int32_t MenuID, void * hParent, uint8_t * HEADER);

private:
    CGfxPlugin(const CGfxPlugin &);
    CGfxPlugin & operator=(const CGfxPlugin &);

    virtual int32_t GetDefaultSettingStartRange() const
    {
        return FirstGfxDefaultSet;
    }
    virtual int32_t GetSettingStartRange() const
    {
        return FirstGfxSettings;
    }
    PLUGIN_TYPE type()
    {
        return PLUGIN_TYPE_VIDEO;
    }

    void UnloadPluginDetails(void);
    bool Initiate_1_4(CN64System * System, RenderWindow * Window);
    bool Initiate_1_5(CN64System * System, RenderWindow * Window);

    static void CALL DummyDrawScreen(void)
    {
    }
    static void CALL DummyMoveScreen(int32_t /*xpos*/, int32_t /*ypos*/)
    {
    }
    static void CALL DummyViStatusChanged(void)
    {
    }
    static void CALL DummyViWidthChanged(void)
    {
    }
    static void CALL DummySoftReset(void)
    {
    }
};