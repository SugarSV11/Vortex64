// The state of a user who has just installed Vortex64, started as the application starts (main.cpp: AppInit, the window, the library
// scanned), in a HOME and in XDG directories that are temporary and made here (the folders of whoever runs the test are never read or written).
// Offscreen Qt:
//
//   first-run-test new      a new installation, no data of Project64: empty library, nothing recent, no favourites, no history, no game
//                           selected, no saves, the defaults of the registration of every setting, no path of the person who runs it
//   first-run-test legacy   an old installation of Project64 (synthetic files): its data is brought once, the old folders stay as they were,
//                           nothing of its games is made up (no favourites, no history of Vortex64)
#include "ui_test_env.h"
#include "../Dialogs/FirstRun.h"

#include <Vortex64-core/Multilanguage.h>
#include <Vortex64-core/N64System/SystemGlobals.h>
#include <Vortex64-plugin-spec/PresentControl.h>

#include <QCryptographicHash>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <pwd.h>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;

static std::string RealHome;

static void Write(const fs::path & Path, const std::string & Text)
{
    fs::create_directories(Path.parent_path());
    std::ofstream(Path, std::ios::binary) << Text;
}

static std::string Read(const fs::path & Path)
{
    std::ifstream In(Path, std::ios::binary);
    std::stringstream S;
    S << In.rdbuf();
    return S.str();
}

// path -> hash of every file under a directory
static std::map<std::string, std::string> Snapshot(const fs::path & Root)
{
    std::map<std::string, std::string> Files;
    if (!fs::exists(Root))
    {
        return Files;
    }
    for (const auto & Entry : fs::recursive_directory_iterator(Root))
    {
        if (Entry.is_regular_file() && !Entry.is_symlink())
        {
            Files[Entry.path().string()] = QCryptographicHash::hash(QByteArray::fromStdString(Read(Entry.path())), QCryptographicHash::Sha256).toHex().toStdString();
        }
    }
    return Files;
}

static bool IsRomName(const std::string & Name)
{
    static const QRegularExpression Rom(QStringLiteral("\\.(z64|n64|v64|ndd|d64|rom|7z|zip)$"), QRegularExpression::CaseInsensitiveOption);
    return Rom.match(QString::fromStdString(Name)).hasMatch();
}

// Nothing the application wrote may carry the home or the name of whoever runs it, or a game: the files that are not the logs
// (a log says where the program is, and stays in the folder of its own user)
static void CheckNoPersonalData(const fs::path & Root, const char * What)
{
    if (!fs::exists(Root))
    {
        return;
    }
    for (const auto & Entry : fs::recursive_directory_iterator(Root))
    {
        const std::string Path = Entry.path().string();
        if (!Entry.is_regular_file() || Entry.is_symlink() || Path.find("/Logs/") != std::string::npos)
        {
            continue;
        }
        if (IsRomName(Entry.path().filename().string()))
        {
            printf("FAIL %s: a game in %s\n", What, Path.c_str());
            g_Failures++;
        }
        if (fs::file_size(Entry.path()) > (4 << 20))
        {
            continue;
        }
        const std::string Text = Read(Entry.path());
        const char * Needles[] = {RealHome.c_str(), "sugarsv", "Escritorio", "ROMS-PARA-PRUEBAS", "test.z64", "B3313", "Super Mario 64"};
        for (const char * Needle : Needles)
        {
            if (Needle[0] != '\0' && Text.find(Needle) != std::string::npos)
            {
                printf("FAIL %s: '%s' in %s\n", What, Needle, Path.c_str());
                g_Failures++;
            }
        }
    }
}

static fs::path MakeSandbox(fs::path & Config, fs::path & Data)
{
    char Template[] = "/tmp/vortex-first-run-XXXXXX";
    const fs::path Root = mkdtemp(Template);
    Config = Root / "config";
    Data = Root / "data";
    fs::create_directories(Root / "home");
    setenv("HOME", (Root / "home").c_str(), 1);
    setenv("XDG_CONFIG_HOME", Config.c_str(), 1);
    setenv("XDG_DATA_HOME", Data.c_str(), 1);
    setenv("XDG_CACHE_HOME", (Root / "cache").c_str(), 1);
    return Root;
}

int main(int argc, char ** argv)
{
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const std::string Mode = argc > 1 ? argv[1] : "new";
    if (Mode != "new" && Mode != "legacy")
    {
        printf("usage: first-run-test new|legacy\n");
        return 2;
    }
    // (what the home of the person is: it must not be anywhere in what is written)
    const char * Home = getenv("HOME");
    RealHome = Home != nullptr ? Home : "";
    if (const passwd * Entry = getpwuid(getuid()))
    {
        if (RealHome.empty())
        {
            RealHome = Entry->pw_dir;
        }
    }
    fs::path Config, Data;
    const fs::path Root = MakeSandbox(Config, Data);
    const fs::path LegacyConfig = Config / "project64", LegacyData = Data / "project64";
    const fs::path Roms = Root / "legacy-roms";
    std::map<std::string, std::string> LegacyBefore;

    if (Mode == "legacy")
    {
        // an old installation: settings, the game folder, a recent game, a save, a state, a screenshot
        fs::create_directories(Roms);
        MakeRom(QString::fromStdString((Roms / "Old Game (USA).z64").string()), "OLDGAME", 'E', 8 << 20);
        Write(LegacyConfig / "Project64.cfg", "[Settings]\nCurrent Language=English\nRemembered Rom Files=10\n\n[Game Directory]\nDirectory=" + Roms.string() + "/\nUse Selected=1\n\n[Recent File]\nRecent Rom 0=" + (Roms / "Old Game (USA).z64").string() + "\n");
        Write(LegacyConfig / "Project64.sc3", "shortcuts");
        Write(LegacyData / "Save" / "OLDGAME.eep", "EEPROM-DATA");
        Write(LegacyData / "Save" / "OLDGAME.pj0", "STATE");
        Write(LegacyData / "Screenshots" / "shot.png", "PNG");
        LegacyBefore = Snapshot(LegacyConfig);
        for (const auto & Item : Snapshot(LegacyData))
        {
            LegacyBefore[Item.first] = Item.second;
        }
    }
    else
    {
        CHECK(!fs::exists(LegacyConfig) && !fs::exists(LegacyData));
    }
    CHECK(!fs::exists(Config / "vortex64") && !fs::exists(Data / "vortex64"));

    QApplication App(argc, argv);
    {
        MainWindow Window;
        const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
        char * Args[] = {argv[0], nullptr};
        if (!AppInit(Window.GetNotify(), Base.c_str(), 1, Args))
        {
            printf("AppInit failed\n");
            return 2;
        }
        Window.CreateLanguageMenus();
        ApplyFirstRunDefaults(&Window); // (as main.cpp does: no wizard, the usual window)
        Window.show();
        Vortex::Shell * Shell = Window.LibraryShell();
        CHECK(Shell != nullptr);
        if (Shell == nullptr)
        {
            return 2;
        }
        Vortex::Store & Store = Vortex::Store::Instance();
        CHECK(Store.DefaultPath() == QString::fromStdString((Config / "vortex64" / "vortex.json").string()));
        CHECK(WaitFor([&]() { return !Shell->Model()->Scanning(); }, 20000));
        QApplication::processEvents();

        // ---- the folders of Vortex64 are the ones that exist; the old ones are Project64's and are not made up
        CHECK(fs::is_directory(Config / "vortex64") && fs::is_directory(Data / "vortex64"));
        if (Mode == "new")
        {
            CHECK(!fs::exists(LegacyConfig) && !fs::exists(LegacyData));
        }

        // ---- the same in both: nothing of Vortex64's own to start with
        CHECK(Store.Favorites().isEmpty());
        CHECK(Store.RecentRoms().isEmpty());
        CHECK(Store.ExtraDirectories().isEmpty());
        CHECK(Store.Filter().isEmpty());
        CHECK(Store.SessionRom().isEmpty());
        CHECK(Store.Page() == "library");
        CHECK(Store.Language() == (Mode == "new" ? "es" : "auto")); // (a new installation starts in Spanish)
        CHECK(QApplication::activeModalWidget() == nullptr && Window.isVisible());
        CHECK(Store.ViewMode() == "grid");
        CHECK(Store.SortKey() == "name" && Store.SortAscending());
        CHECK(Shell->Page("favorites")->Proxy()->rowCount() == 0);
        CHECK(Shell->Page("recent")->Proxy()->rowCount() == 0);
        CHECK(Shell->Page("favorites")->Browser()->EmptyShown());
        CHECK(Shell->Page("recent")->Browser()->EmptyShown());
        CHECK(Shell->CurrentPage() == "library");

        // ---- the defaults are those of the registration of every setting (a default is never copied here: it is asked of the core)
        CHECK(g_Settings->LoadDword(UserInterface_VSyncMode) == PJ64_VSYNC_AUTO);
        CHECK(g_Settings->LoadDword(UserInterface_MaxFps) == 0);
        CHECK(g_Settings->LoadBool(Setting_AutoStart) == g_Settings->LoadDefaultBool(Setting_AutoStart));
        CHECK(!g_Settings->LoadBool(Setting_ForceInterpreterCPU));
        CHECK(!g_Settings->LoadBool(Setting_Enhancement) || g_Settings->LoadBool(Setting_Enhancement) == g_Settings->LoadDefaultBool(Setting_Enhancement));
        CHECK(UISettingsLoadDword(File_RecentGameFileCount) == 10);
        CHECK(Mode == "legacy" || UISettingsLoadStringIndex(File_RecentGameFileIndex, 0).empty());
        CHECK(UISettingsLoadStringIndex(Directory_RecentGameDirIndex, 0).empty() || Mode == "legacy");
        CHECK(!g_Settings->LoadBool(Directory_SnapShotUseSelected) && !g_Settings->LoadBool(Directory_NativeSaveUseSelected) && !g_Settings->LoadBool(Directory_InstantSaveUseSelected));
        CHECK(!g_Settings->LoadBool(Directory_TextureUseSelected) && !g_Settings->LoadBool(Directory_PluginUseSelected) && !g_Settings->LoadBool(Directory_LogUseSelected));
        // the plugins: the ones that exist next to the executable
        for (SettingID Plugin : {Plugin_GFX_Current, Plugin_AUDIO_Current, Plugin_CONT_Current, Plugin_RSP_Current})
        {
            const std::string Name = g_Settings->LoadStringVal(Plugin);
            CHECK(!Name.empty());
            CPath File(g_Settings->LoadStringVal(Directory_Plugin).c_str(), Name.c_str());
            CHECK(File.Exists());
        }

        if (Mode == "new")
        {
            // ---- no library at all: no game, none selected, no state, no save
            CHECK(Shell->Model()->Count() == 0);
            CHECK(Shell->Page("library")->Proxy()->rowCount() == 0);
            CHECK(Shell->Disks()->Proxy()->rowCount() == 0);
            CHECK(Shell->Page("library")->Browser()->EmptyShown());
            CHECK(Shell->SelectedRom().isEmpty() && Store.LastSelected().isEmpty());
            CHECK(g_Lang->IsLanguageLoaded() && g_Settings->LoadStringVal(Setting_CurrentLanguage) == "Español"); // (nothing is asked: Spanish from the start)
            CHECK(g_Settings->LoadStringVal(Plugin_GFX_Current) == (QFile::exists(QCoreApplication::applicationDirPath() + "/libGLideN64.so") ? "libGLideN64.so" : "libVortex64-video.so"));
            CHECK(g_Settings->LoadStringVal(Cmd_RomFile).empty());
            CHECK(!g_Settings->LoadBool(RomList_GameDirUseSelected));
            CHECK(!g_Settings->LoadBool(RomList_GameDirRecursive));
            const QString GameDir = QString::fromStdString(g_Settings->LoadStringVal(RomList_GameDir));
            CHECK(!QDir(GameDir).exists() || QDir(GameDir).entryList(QDir::Files).isEmpty());
            // (the registered default is a folder next to the program that does not exist: nothing of the person, nothing to scan)
            CHECK(GameDir.startsWith(QString::fromStdString(CPath(CPath::MODULE_DIRECTORY).GetDirectory())) && GameDir.endsWith("Game Directory/"));
        }
        else
        {
            // ---- the old game folder is the library, what was recent is recent in the core, nothing is a favourite or has been played
            CHECK(g_Lang->IsLanguageLoaded());
            CHECK(Shell->Model()->Count() == 1);
            CHECK(Shell->Page("library")->Proxy()->rowCount() == 1);
            CHECK(UISettingsLoadStringIndex(File_RecentGameFileIndex, 0) == (Roms / "Old Game (USA).z64").string());
            CHECK(Read(Config / "vortex64" / "Project64.sc3") == "shortcuts");
            CHECK(Read(Data / "vortex64" / "Save" / "OLDGAME.eep") == "EEPROM-DATA" && Read(Data / "vortex64" / "Save" / "OLDGAME.pj0") == "STATE");
            CHECK(Read(Data / "vortex64" / "Screenshots" / "shot.png") == "PNG");
            CHECK(fs::exists(Config / "vortex64" / ".migrated-from-project64") && fs::exists(Data / "vortex64" / ".migrated-from-project64"));
        }

        Store.Flush();
        g_Settings->SaveBool(UserInterface_BasicMode, g_Settings->LoadBool(UserInterface_BasicMode)); // (the settings are written as the program ends)
    }
    AppCleanup();

    if (Mode == "new")
    {
        // ---- what the first run left on disk: no game, no save, no personal path anywhere
        const auto Written = Snapshot(Config);
        const auto WrittenData = Snapshot(Data);
        int Saves = 0;
        if (fs::exists(Data / "vortex64" / "Save"))
        {
            for (const auto & Entry : fs::recursive_directory_iterator(Data / "vortex64" / "Save"))
            {
                Saves += Entry.is_regular_file() ? 1 : 0;
            }
        }
        CHECK(Saves == 0);
        // the store, when it was written, is empty
        if (fs::exists(Config / "vortex64" / "vortex.json"))
        {
            const QJsonObject Json = QJsonDocument::fromJson(QByteArray::fromStdString(Read(Config / "vortex64" / "vortex.json"))).object();
            CHECK(Json.value("favorites").toArray().isEmpty());
            CHECK(Json.value("plays").toObject().isEmpty());
            CHECK(!Json.contains("lastSelected") || Json.value("lastSelected").toString().isEmpty());
        }
        printf("first run (new): %zu files in the configuration, %zu in the data\n", Written.size(), WrittenData.size());
        for (const auto & Item : Written)
        {
            printf("  config: %s\n", fs::relative(Item.first, Config).c_str());
        }
        for (const auto & Item : WrittenData)
        {
            printf("  data: %s\n", fs::relative(Item.first, Data).c_str());
        }
    }
    else
    {
        // ---- the old folders are exactly as they were
        auto After = Snapshot(LegacyConfig);
        for (const auto & Item : Snapshot(LegacyData))
        {
            After[Item.first] = Item.second;
        }
        CHECK(After == LegacyBefore);
    }
    CheckNoPersonalData(Config / "vortex64", "configuration");
    CheckNoPersonalData(Data / "vortex64", "data");

    fs::remove_all(Root);
    printf(g_Failures == 0 ? "PASS first-run %s\n" : "FAILED first-run %s\n", Mode.c_str());
    return g_Failures == 0 ? 0 : 1;
}
