// What the tests of the interface of Vortex64 share: the checks, the games that are fixtures (header-only ROMs and disks) and the
// application of the tests (a MainWindow with the core initialised, isolated XDG directories, offscreen Qt).
#pragma once

#include "../Debugger/DebuggerUI.h"
#include "../MainMenu.h"
#include "../MainWindow.h"
#include "../Settings/SettingsDialog.h"
#include "../Vortex/Shell.h"
#include "../Vortex/Store.h"

#include <Common/path.h>
#include <Vortex64-core/AppInit.h>
#include <Vortex64-core/Settings.h>
#include <Vortex64-core/Settings/DebugSettings.h>
#include <Vortex64-ui/Settings/UISettings.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>
#include <functional>

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

static bool WaitFor(const std::function<bool()> & Condition, int TimeoutMs)
{
    QElapsedTimer Timer;
    Timer.start();
    while (Timer.elapsed() < TimeoutMs)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        if (Condition())
        {
            return true;
        }
        QThread::msleep(10);
    }
    return Condition();
}

static void MakeRom(const QString & Path, const char * Title, char Country, qint64 Size)
{
    QFile File(Path);
    File.open(QIODevice::WriteOnly);
    QByteArray Head(0x1000, 0);
    Head[0] = char(0x80);
    Head[1] = 0x37;
    Head[2] = 0x12;
    Head[3] = 0x40;
    for (int i = 0; i < 8; i++)
    {
        Head[0x10 + i] = char(Title[0] + i * 3);
    }
    memcpy(Head.data() + 0x20, Title, qMin<size_t>(strlen(Title), 20));
    Head[0x3B] = 'N';
    Head[0x3C] = 'X';
    Head[0x3D] = 'X';
    Head[0x3E] = Country;
    File.write(Head);
    File.resize(Size);
}

static void MakeDisk(const QString & Path)
{
    QFile File(Path);
    File.open(QIODevice::WriteOnly);
    QByteArray Head(0x1000, 0);
    Head[6] = 0x00;
    Head[7] = 0x10;
    Head[0x1C] = char(0x80);
    Head[0x1D] = 0x10;
    File.write(Head);
    File.resize(0x03DEC800);
}

// A library of fixtures in a temporary directory, the store there, the window with the core initialised and the library scanned.
// Returns false (and prints why) when something does not start.
struct UiTestApp
{
    QTemporaryDir Work;
    QString Roms;
    QString StorePath;
    MainWindow * Window = nullptr;
    CDebuggerUI * Debugger = nullptr; // (main() makes it as well: the debugger always exists)

    bool Start(const char * Argv0, const QString & LongTitleGame = QString())
    {
        if (!Work.isValid())
        {
            return false;
        }
        Roms = Work.path() + "/roms";
        QDir().mkpath(Roms);
        MakeRom(Roms + "/Super Mario 64 (USA).z64", "SUPER MARIO 64", 'E', 8 << 20);
        MakeRom(Roms + "/Mario Kart 64 (USA).z64", "MARIOKART64", 'E', 12 << 20);
        MakeRom(Roms + "/Star Fox 64 (Europe).z64", "STARFOX64", 'P', 12 << 20);
        MakeRom(Roms + "/Mario Party 2 (Japan).z64", "MARIOPARTY2", 'J', 32 << 20);
        MakeDisk(Roms + "/F-Zero X Expansion (Japan).ndd");
        if (!LongTitleGame.isEmpty())
        {
            MakeRom(Roms + "/" + LongTitleGame + ".z64", "LONG", 'E', 8 << 20);
        }
        StorePath = Work.path() + "/vortex64/vortex.json";
        Vortex::Store::SetPath(StorePath);
        Window = new MainWindow;
        const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
        char * Args[] = {const_cast<char *>(Argv0), nullptr};
        if (!AppInit(Window->GetNotify(), Base.c_str(), 1, Args))
        {
            printf("AppInit failed\n");
            return false;
        }
        g_Settings->SaveString(RomList_GameDir, (Roms + "/").toStdString());
        g_Settings->SaveBool(UserInterface_BasicMode, false);
        g_Settings->SaveBool(Debugger_Enabled, true);
        Debugger = new CDebuggerUI;
        Window->CreateLanguageMenus();
        Window->show();
        return Window->LibraryShell() != nullptr && WaitFor([&]() { return Window->LibraryShell()->Model()->Count() >= 5 && !Window->LibraryShell()->Model()->Scanning(); }, 20000);
    }
};
