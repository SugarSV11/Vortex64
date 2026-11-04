// Vortex64: the Qt6 frontend of the Project64 core on Linux.
//
// See MainWindow.h/.cpp and QtNotification.h/.cpp for the window, embedded
// video, runtime controls, and thread-safe CNotification implementation.
#include "MainWindow.h"
#include "Dialogs/FirstRun.h"
#include "Debugger/DebuggerUI.h"
#include "Vortex/Icons.h"
#include "Vortex/ShotScript.h"
#include "Vortex/Store.h"
#include "Vortex/Version.h"
#include "Vortex/Strings.h"
#include "Vortex/Theme.h"

#include <Common/path.h>
#include <Project64-core/AppInit.h>
#include <Project64-core/Multilanguage.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/N64System/N64System.h>

#include <QMessageBox>
#include <Common/Util.h>
#include <QApplication>
#include <QIcon>
#include <QTimer>

#include <X11/Xlib.h>

#include <csignal>
#include <cstdlib>

// SIGINT/SIGTERM close the main window like the window manager would (polled by a
// timer: nothing but setting a flag is async-signal-safe)
static volatile sig_atomic_t g_QuitRequested = 0;

static void OnQuitSignal(int)
{
    g_QuitRequested = 1;
}

int main(int argc, char ** argv)
{
    signal(SIGINT, OnQuitSignal);
    signal(SIGTERM, OnQuitSignal);
    // Force both Qt and the video plugin's SDL2 backend onto real X11
    // windows rather than letting either pick a native Wayland surface.
    // This desktop is a Wayland session (XDG_SESSION_TYPE=wayland), and Qt6
    // defaults to its "wayland" platform plugin whenever WAYLAND_DISPLAY is
    // set unless told otherwise - which silently broke window
    // discoverability with X11-only tooling (xwininfo/wmctrl saw only the
    // video plugin's SDL/XWayland window, not this MainWindow, even though
    // both existed) and would have made GetVideoWindowHandle()'s
    // documented "real X11 Window XID" contract false once embedding is
    // re-enabled (see MainWindow.cpp), since a native Wayland QWidget's
    // winId() isn't an X11 XID at all. setenv()'s overwrite=0 leaves an
    // explicit caller override (e.g. QT_QPA_PLATFORM=wayland to test that
    // path deliberately) intact.
    setenv("QT_QPA_PLATFORM", "xcb", 0);
    setenv("SDL_VIDEODRIVER", "x11", 0);

    // Qt's xcb platform plugin and the video plugin's SDL2 x11 backend
    // (Project64-video/Renderer/OGLglitchmain.cpp, forced via
    // SDL_VIDEODRIVER=x11 - see LINUX_PORT.md) both end up touching Xlib in
    // the same process once a ROM actually runs, and SDL creates its
    // window on CN64System's own emulation thread (see
    // CN64System::StartEmulationThread()), not this one - i.e. two
    // separate Xlib-using connections, one of them cross-thread from the
    // other's perspective. Standard practice is to call XInitThreads()
    // before any other Xlib call in the process in exactly this situation.
    // (For the record: this was investigated as the suspected cause of a
    // real crash in gfxSstWinOpen() while loading GL extension function
    // pointers, but wasn't it - see LINUX_PORT.md for the actual root
    // cause, a GL symbol name collision unrelated to threading. Kept
    // anyway as the textbook-correct thing to do here, not because it
    // fixed that specific bug.) Must run before QApplication's own
    // constructor, which is the first thing in this process to touch Xlib.
    XInitThreads();

    QApplication app(argc, argv);
    QApplication::setApplicationName("Vortex64");
    QApplication::setApplicationDisplayName("Vortex64");
    QApplication::setApplicationVersion(VORTEX_VERSION_STR);
    Vortex::ApplyTheme(app);
    // the icon of the windows (IDI_PJ64_Icon of upstream) and the desktop entry that the window
    // belongs to (project64.desktop), which Wayland compositors use to find the icon
    QApplication::setWindowIcon(Vortex::AppIcon());
    QApplication::setDesktopFileName("vortex64");
    // Gives QSettings (see MainWindow.cpp's Recent ROMs list) a real path:
    // $XDG_CONFIG_HOME/Project64/Project64.conf. This is deliberately a
    // separate file from Project64.cfg (Project64-core's own settings,
    // redirected to $XDG_CONFIG_HOME/project64/Project64.cfg - see
    // SettingsType-RelativePath.cpp) rather than another core setting,
    // mirroring how the Windows GUI already keeps its own UI-only state
    // (UISettings.cpp's "Recent Rom" MRU list, etc.) separate from the
    // core's settings rather than adding it there.
    QApplication::setOrganizationName("Vortex64");

    MainWindow window;

    // Same reasoning as Project64-cli/main.cpp: CPath::MODULE_DIRECTORY
    // resolves via /proc/self/exe and works before AppInit()/g_Settings
    // exist, and AppInit() can't take a null/empty BaseDirectory (it ends
    // up wrapped in a plain std::string internally).
    const std::string BaseDirectory = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
    if (!AppInit(window.GetNotify(), BaseDirectory.c_str(), argc, argv))
    {
        return 1;
    }

    // Setting_CheckEmuRunning (CheckEmuRunning of upstream's AppInit): another copy that is running is ended
    // (after asking), and the settings are read again because that copy writes them as it ends
    if (g_Settings->LoadBool(Setting_CheckEmuRunning))
    {
        const int Other = pjutil::ExistingExePid(); // (0, and this copy has the lock, when there is no other)
        if (Other > 0)
        {
            if (QMessageBox::question(nullptr, Vortex::Tr("Cerrar Vortex64", "Terminate Vortex64"), Vortex::Tr("Vortex64 ya se está ejecutando\n\n¿Cerrar el proceso %1 ahora?", "Vortex64 is currently running\n\nTerminate PID %1 now?").arg(Other), QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes &&
                pjutil::TerminateExe(Other))
            {
                AppCleanup();
                if (!AppInit(window.GetNotify(), BaseDirectory.c_str(), argc, argv))
                {
                    return 1;
                }
                pjutil::RegisterRunningExe();
            }
        }
        else
        {
            pjutil::RegisterRunningExe();
        }
    }

    // Like upstream's WinMain the debugger object always exists (its windows are opened
    // from the Debugger menu, which only exists with the debugger enabled)
    CDebuggerUI * Debugger = new CDebuggerUI;
    if (g_DebugSettings.haveDebugger)
    {
        Debugger->StartAutorunScripts();
    }

    QTimer * QuitTimer = new QTimer(&window);
    QObject::connect(QuitTimer, &QTimer::timeout, &window, [&window]() {
        if (g_QuitRequested)
        {
            // a modal dialog (Settings, ...) has its own event loop that would keep the
            // application from ending
            while (QWidget * Modal = QApplication::activeModalWidget())
            {
                Modal->close();
            }
            // the request stays until the window is there to be closed
            if (window.isVisible())
            {
                g_QuitRequested = 0;
                window.close();
            }
        }
    });
    QuitTimer->start(200);

    window.CreateLanguageMenus();
    // First run: no wizard, the defaults are saved and the usual window opens
    ApplyFirstRunDefaults(&window);
    window.InitPlugins();
    window.show();
    Vortex::RunShotScriptFromEnvironment(&window);

    // Support `vortex64 <rom>` and packaging/linux/vortex64.desktop's
    // Exec=vortex64 %f (a file manager's "Open With" launches this
    // way) - Qt already stripped its own recognized options out of argv,
    // so any remaining non-flag argument is the ROM path.
    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] != '-')
        {
            // Deferred to the event loop's first iteration (QTimer::singleShot
            // with a 0ms delay) rather than called directly here, so the
            // window has actually been shown/painted before this
            // synchronously blocks on loading and running the ROM.
            const QString romPath = QString::fromLocal8Bit(argv[i]);
            QTimer::singleShot(0, &window, [&window, romPath]()
                                { window.OpenRomPath(romPath); });
            break;
        }
    }

    const int result = app.exec();

    CN64System::CloseSystem();
    delete Debugger;
    AppCleanup();
    return result;
}
