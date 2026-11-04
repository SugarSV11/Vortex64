// Plugin loading test (CPlugins over dlopen/dlsym): discovery of the four plugins,
// interface/type validation, load errors, selecting another plugin and repeated
// load/unload cycles. No window needed.
#include <Common/path.h>
#include <Project64-cli/LinuxNotification.h>
#include <Project64-core/AppInit.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Plugins/AudioPlugin.h>
#include <Project64-core/Plugins/ControllerPlugin.h>
#include <Project64-core/Plugins/GFXPlugin.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/Plugins/RSPPlugin.h>
#include <Project64-core/Settings.h>

#include <cstdio>

static int g_Failures = 0;
#define CHECK(cond)                                                            \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            g_Failures++;                                                      \
        }                                                                      \
    } while (0)

class DummyRenderWindow : public RenderWindow
{
public:
    void * GetWindowHandle(void) const override { return nullptr; }
};

static void Recreate(DummyRenderWindow & Window)
{
    delete g_Plugins;
    g_Plugins = new CPlugins(Directory_Plugin, false);
    g_Plugins->SetRenderWindows(&Window, &Window);
    g_Plugins->CreatePlugins();
}

int main(int, char ** argv)
{
    static CLinuxNotification Notify;
    const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
    if (!AppInit(&Notify, Base.c_str(), 1, argv))
    {
        return 2;
    }
    g_Settings->SaveString(Directory_Plugin, Base);
    static DummyRenderWindow Window;

    // all four load and report their type
    Recreate(Window);
    CHECK(g_Plugins->Gfx() != nullptr && g_Plugins->Audio() != nullptr && g_Plugins->Control() != nullptr && g_Plugins->RSP() != nullptr);
    CHECK(g_Plugins->Gfx() != nullptr && g_Plugins->Gfx()->DllConfig != nullptr);
    CHECK(g_Plugins->Audio() != nullptr && g_Plugins->Audio()->DllConfig != nullptr);
    CHECK(g_Plugins->Control() != nullptr && g_Plugins->Control()->DllConfig != nullptr);

    // a plugin of the wrong type is refused (audio plugin selected as video plugin)
    g_Settings->SaveString(Plugin_GFX_Current, "libProject64-audio.so");
    Recreate(Window);
    CHECK(g_Plugins->Gfx() == nullptr);
    CHECK(g_Plugins->Audio() != nullptr && g_Plugins->Control() != nullptr && g_Plugins->RSP() != nullptr);

    // a file that is not there
    g_Settings->SaveString(Plugin_GFX_Current, "libDoesNotExist.so");
    Recreate(Window);
    CHECK(g_Plugins->Gfx() == nullptr);

    // a shared object without the plugin entry points
    g_Settings->SaveString(Plugin_GFX_Current, "libProject64-video-qtconfig.so");
    Recreate(Window);
    CHECK(g_Plugins->Gfx() == nullptr);

    // a file that is not an ELF object at all
    g_Settings->SaveString(Plugin_GFX_Current, "Project64.rdb");
    Recreate(Window);
    if (g_Plugins->Gfx() != nullptr) { printf("loaded: %s\n", g_Plugins->Gfx()->PluginName()); }
    CHECK(g_Plugins->Gfx() == nullptr);

    // back to the valid selection, then repeated load/unload cycles
    g_Settings->SaveString(Plugin_GFX_Current, "libProject64-video.so");
    for (int i = 0; i < 5; i++)
    {
        Recreate(Window);
        CHECK(g_Plugins->Gfx() != nullptr && g_Plugins->Audio() != nullptr && g_Plugins->Control() != nullptr && g_Plugins->RSP() != nullptr);
    }
    delete g_Plugins;
    g_Plugins = nullptr;
    AppCleanup();
    printf(g_Failures == 0 ? "PASS\n" : "FAILED\n");
    return g_Failures == 0 ? 0 : 1;
}
