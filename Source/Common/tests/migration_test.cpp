// The move from the folders of Project64 to those of Vortex64 ($XDG_CONFIG_HOME/project64 -> /vortex64, the same for the data):
// a new installation, an existing one (settings, saves, states, screenshots, cheats, scripts, the plugin's settings), what is not copied
// (logs; the textures get a link), nothing of the old folders changed, once only, never over an existing file, and a copy that fails is
// tried again. Every path is under a temporary directory: the folders of the user are not touched.
#include "../path.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
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

static void Write(const fs::path & Path, const std::string & Text)
{
    fs::create_directories(Path.parent_path());
    std::ofstream(Path) << Text;
}
static std::string Read(const fs::path & Path)
{
    std::ifstream In(Path);
    std::stringstream S;
    S << In.rdbuf();
    return S.str();
}

static void Populate(const fs::path & Config, const fs::path & Data)
{
    Write(Config / "Project64.cfg", "[Settings]\nCurrent Language=English\n");
    Write(Config / "Project64.sc3", "shortcuts");
    Write(Config / "GLideN64" / "GLideN64.ini", "[General]\nversion=29\n");
    Write(Config / "Config" / "Project64.rdb", "user rdb");
    Write(Data / "Save" / "game.eep", "EEPROM-DATA");
    Write(Data / "Save" / "game.pj0", "STATE");
    Write(Data / "Save" / "sub" / "game.mpk", "PAK");
    Write(Data / "Screenshots" / "shot.png", "PNG");
    Write(Data / "Cheats-User" / "user.cht", "cheat");
    Write(Data / "Scripts" / "s.js", "script");
    Write(Data / "Logs" / "old.log", "log");
    Write(Data / "Textures" / "pack" / "tex.png", "texture");
}

static void Environment(const fs::path & Root)
{
    setenv("XDG_CONFIG_HOME", (Root / "config").c_str(), 1);
    setenv("XDG_DATA_HOME", (Root / "data").c_str(), 1);
}

int main()
{
    char Template[] = "/tmp/vortex-migration-XXXXXX";
    const fs::path Root = mkdtemp(Template);
    // the names of the folders
    Environment(Root / "x");
    CHECK(CPath::UserConfigDir() == (Root / "x" / "config" / "vortex64").string() && CPath::LegacyConfigDir() == (Root / "x" / "config" / "project64").string());
    CHECK(CPath::UserDataDir() == (Root / "x" / "data" / "vortex64").string() && CPath::LegacyDataDir() == (Root / "x" / "data" / "project64").string());

    // 1. a new installation: nothing to bring, nothing created of the old name, the work is marked as done
    {
        Environment(Root / "new");
        CHECK(CPath::MigrateLegacyUserData() == 0);
        CHECK(!fs::exists(CPath::LegacyConfigDir()) && !fs::exists(CPath::LegacyDataDir()));
        CHECK(fs::exists(fs::path(CPath::UserConfigDir()) / ".migrated-from-project64"));
        // an old folder that shows up later is not imported (the decision was made)
        Write(fs::path(CPath::LegacyConfigDir()) / "Project64.cfg", "late");
        CHECK(CPath::MigrateLegacyUserData() == 0 && !fs::exists(fs::path(CPath::UserConfigDir()) / "Project64.cfg"));
    }

    // 2. an existing installation
    {
        Environment(Root / "old");
        const fs::path OldConfig = CPath::LegacyConfigDir(), OldData = CPath::LegacyDataDir();
        Populate(OldConfig, OldData);
        struct stat Before;
        stat((OldData / "Save" / "game.eep").c_str(), &Before);
        std::string Report;
        const int Copied = CPath::MigrateLegacyUserData(&Report);
        const fs::path NewConfig = CPath::UserConfigDir(), NewData = CPath::UserDataDir();
        printf("migration: %d files copied\n%s", Copied, Report.c_str());
        CHECK(Copied == 10);
        CHECK(Read(NewConfig / "Project64.cfg") == "[Settings]\nCurrent Language=English\n");
        CHECK(Read(NewConfig / "Project64.sc3") == "shortcuts" && Read(NewConfig / "GLideN64" / "GLideN64.ini") == "[General]\nversion=29\n" && Read(NewConfig / "Config" / "Project64.rdb") == "user rdb");
        CHECK(Read(NewData / "Save" / "game.eep") == "EEPROM-DATA" && Read(NewData / "Save" / "game.pj0") == "STATE" && Read(NewData / "Save" / "sub" / "game.mpk") == "PAK");
        CHECK(Read(NewData / "Screenshots" / "shot.png") == "PNG" && Read(NewData / "Cheats-User" / "user.cht") == "cheat" && Read(NewData / "Scripts" / "s.js") == "script");
        // not copied: logs; the textures are a link to where they are
        CHECK(!fs::exists(NewData / "Logs"));
        CHECK(fs::is_symlink(NewData / "Textures") && Read(NewData / "Textures" / "pack" / "tex.png") == "texture");
        CHECK(fs::exists(NewConfig / ".migrated-from-project64") && fs::exists(NewData / ".migrated-from-project64"));
        // nothing of the old folders changed or removed
        struct stat After;
        stat((OldData / "Save" / "game.eep").c_str(), &After);
        CHECK(Read(OldData / "Save" / "game.eep") == "EEPROM-DATA" && Before.st_mtime == After.st_mtime && Before.st_ino == After.st_ino);
        CHECK(fs::exists(OldConfig / "Project64.cfg") && fs::exists(OldData / "Logs" / "old.log") && fs::exists(OldData / "Textures" / "pack" / "tex.png"));
        // once: the second time nothing is copied, and what the user changed since is not touched
        Write(NewConfig / "Project64.cfg", "changed in Vortex64");
        Write(OldConfig / "Project64.cfg", "changed in the old one");
        CHECK(CPath::MigrateLegacyUserData() == 0);
        CHECK(Read(NewConfig / "Project64.cfg") == "changed in Vortex64");
    }

    // 3. never over a file that exists in the new folder (a marker that is not there yet)
    {
        Environment(Root / "clash");
        Populate(CPath::LegacyConfigDir(), CPath::LegacyDataDir());
        Write(fs::path(CPath::UserConfigDir()) / "Project64.cfg", "already here");
        Write(fs::path(CPath::UserDataDir()) / "Save" / "game.eep", "NEW SAVE");
        CPath::MigrateLegacyUserData();
        CHECK(Read(fs::path(CPath::UserConfigDir()) / "Project64.cfg") == "already here");
        CHECK(Read(fs::path(CPath::UserDataDir()) / "Save" / "game.eep") == "NEW SAVE");
        CHECK(Read(fs::path(CPath::UserDataDir()) / "Save" / "game.pj0") == "STATE"); // (the others come)
    }

    // 4. a copy that fails is tried again, and what was copied is not copied twice
    if (geteuid() != 0)
    {
        Environment(Root / "retry");
        Populate(CPath::LegacyConfigDir(), CPath::LegacyDataDir());
        const fs::path Locked = fs::path(CPath::LegacyDataDir()) / "Save" / "game.eep";
        chmod(Locked.c_str(), 0);
        std::string Report;
        CPath::MigrateLegacyUserData(&Report);
        CHECK(!fs::exists(fs::path(CPath::UserDataDir()) / ".migrated-from-project64")); // (not finished: it will be tried again)
        CHECK(Report.find("not finished") != std::string::npos);
        CHECK(!fs::exists(fs::path(CPath::UserDataDir()) / "Save" / "game.eep.migrating") && !fs::exists(fs::path(CPath::UserDataDir()) / "Save" / "game.eep"));
        chmod(Locked.c_str(), 0644);
        const int Second = CPath::MigrateLegacyUserData();
        CHECK(Second >= 1 && Read(fs::path(CPath::UserDataDir()) / "Save" / "game.eep") == "EEPROM-DATA");
        CHECK(fs::exists(fs::path(CPath::UserDataDir()) / ".migrated-from-project64"));
    }

    // 5. several instances that start at once: every file whole, once, the mark written, no half file left over
    {
        Environment(Root / "race");
        Populate(CPath::LegacyConfigDir(), CPath::LegacyDataDir());
        std::string Big(24 << 20, 'x');
        for (size_t i = 0; i < Big.size(); i += 4096)
        {
            Big[i] = (char)('a' + (i / 4096) % 26);
        }
        Write(fs::path(CPath::LegacyDataDir()) / "Save" / "big.sra", Big);
        pid_t Children[6];
        for (pid_t & Child : Children)
        {
            Child = fork();
            if (Child == 0)
            {
                CPath::MigrateLegacyUserData();
                _exit(0);
            }
        }
        for (pid_t Child : Children)
        {
            int Status = 0;
            waitpid(Child, &Status, 0);
            CHECK(WIFEXITED(Status) && WEXITSTATUS(Status) == 0);
        }
        CHECK(Read(fs::path(CPath::UserDataDir()) / "Save" / "big.sra") == Big);
        CHECK(Read(fs::path(CPath::UserDataDir()) / "Save" / "game.eep") == "EEPROM-DATA");
        CHECK(fs::exists(fs::path(CPath::UserDataDir()) / ".migrated-from-project64") && fs::exists(fs::path(CPath::UserConfigDir()) / ".migrated-from-project64"));
        int Leftovers = 0;
        for (const auto & Entry : fs::recursive_directory_iterator(CPath::UserDataDir()))
        {
            Leftovers += Entry.path().string().find(".migrating") != std::string::npos ? 1 : 0;
        }
        CHECK(Leftovers == 0);
    }

    // 6. a relative XDG variable is not valid (the specification): the folders of the home are used, as everywhere else
    {
        setenv("XDG_CONFIG_HOME", "relative/config", 1);
        setenv("XDG_DATA_HOME", "relative/data", 1);
        setenv("HOME", (Root / "home").c_str(), 1);
        CHECK(CPath::UserConfigDir() == (Root / "home" / ".config" / "vortex64").string());
        CHECK(CPath::UserDataDir() == (Root / "home" / ".local" / "share" / "vortex64").string());
    }

    fs::remove_all(Root);
    printf(g_Failures == 0 ? "PASS migration\n" : "FAILED migration\n");
    return g_Failures == 0 ? 0 : 1;
}
