// The check of the plugin directory (Vortex/PluginDirCheck.h): the plugins of another folder that are not the ones of the application are found.
//
//   plugindir-test
#include "../Vortex/PluginDirCheck.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

static int g_Failures = 0;
#define CHECK(cond)                                                \
    do                                                             \
    {                                                              \
        if (!(cond))                                               \
        {                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_Failures++;                                          \
        }                                                          \
    } while (0)

static void Make(const QString & Dir, const char * Name, const QByteArray & Content)
{
    QDir().mkpath(Dir);
    QFile File(Dir + "/" + Name);
    File.open(QIODevice::WriteOnly);
    File.write(Content);
}

int main(int argc, char ** argv)
{
    QCoreApplication App(argc, argv);
    QTemporaryDir Work;
    const QString Own = Work.path() + "/own", Old = Work.path() + "/old", Same = Work.path() + "/same";
    for (const char * Name : {"libVortex64-video.so", "libVortex64-input.so", "libVortex64-rsp.so"})
    {
        Make(Own, Name, QByteArray("current ") + Name);
        Make(Same, Name, QByteArray("current ") + Name);
        Make(Old, Name, QByteArray("older ") + Name);
    }
    Make(Old, "libVortex64-audio.so", "audio only there");  // (not in the own folder: not compared)
    Make(Old, "libThirdParty.so", "somebody else's");       // (a plugin of another author)
    Make(Same, "libThirdParty.so", "somebody else's");

    CHECK(Vortex::PluginsDifferentFromOwn(Own, Own).isEmpty());               // the folder of the application itself
    CHECK(Vortex::PluginsDifferentFromOwn(Own + "/../own", Own).isEmpty());   // (the same folder written another way)
    CHECK(Vortex::PluginsDifferentFromOwn(Same, Own).isEmpty());              // a copy of the same plugins
    CHECK(Vortex::PluginsDifferentFromOwn(Work.path() + "/missing", Own).isEmpty());
    const QStringList Different = Vortex::PluginsDifferentFromOwn(Old, Own);
    CHECK(Different.size() == 3 && Different.contains("libVortex64-video.so") && Different.contains("libVortex64-input.so") && Different.contains("libVortex64-rsp.so"));
    CHECK(!Different.contains("libVortex64-audio.so") && !Different.contains("libThirdParty.so"));
    // the same size, another content
    Make(Same, "libVortex64-video.so", "CURRENT libVortex64-video.so");
    CHECK(Vortex::PluginsDifferentFromOwn(Same, Own) == QStringList({"libVortex64-video.so"}));
    printf("%s plugindir\n", g_Failures == 0 ? "PASS" : "FAILED");
    return g_Failures == 0 ? 0 : 1;
}
