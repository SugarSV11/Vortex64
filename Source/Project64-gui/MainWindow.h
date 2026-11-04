#pragma once

#include <QMainWindow>
#include <QPoint>
#include <QList>
#include <QTimer>
#include <QUrl>
#include <QString>
#include <QStringList>
#include <functional>
#include <utility>
#include <vector>

class QLabel;
class QAction;
class QCloseEvent;
class QMenu;
class QStackedWidget;
class QToolButton;
class QtRomBrowser;
class QtMainMenu;
class QtNotification;
class CNotification;
class ScreenSaverInhibitor;
class QDragEnterEvent;
class QDropEvent;
class QUrl;
class QMoveEvent;
class QResizeEvent;
namespace Vortex
{
class Shell;
}

// Native Qt6 main window with ROM loading and runtime controls. The video
// plugin renders into the central native widget through the
// GetVideoWindowHandle()/SDL_CreateWindowFrom() path under X11/Xwayland.
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget * parent = nullptr);
    ~MainWindow() override;

    // Built in the constructor (needs `this`, which is why AppInit() is
    // called from main.cpp only after the MainWindow itself is fully
    // constructed - see main.cpp).
    CNotification * GetNotify() const;

    // Creates the menus whose text comes from the language files; call after
    // AppInit() succeeded (g_Lang does not exist before that).
    void CreateLanguageMenus();

    // Loads the selected plugins; call after AppInit() succeeded.
    void InitPlugins();

    // The video plugin's real native window handle (an X11 Window ID under
    // the x11 platform plugin - see MainWindow.cpp's constructor comment on
    // why SDL_VIDEODRIVER=x11 matters here) for CQtRenderWindow::
    // GetWindowHandle() to hand to the core, which passes it to the video
    // plugin's InitiateGFX() as GFX_INFO::hWnd (see Plugin.h/GFXPlugin.cpp).
    void * GetVideoWindowHandle() const;

    // Called by QtNotification (from the GUI thread only - see its own
    // thread-marshaling) to reflect emulator status in the window.
    // The two panes of the status bar (CMainGui::SetStatusText): 0 the message, 1 the small one
    void ShowStatusMessage(const QString & message, int timeoutMs);
    void ShowStatusMessage2(const QString & message);
    QLabel * StatusPane(int Pane) const
    {
        return Pane == 0 ? m_StatusLabel : m_StatusLabel2;
    }
    // CNotificationImp::WindowMode: leaves the full screen before a dialog is shown
    void LeaveFullScreen();
    // WM_SETFOCUS/WM_KILLFOCUS of the main window and WM_ACTIVATEAPP (Setting_AutoSleep)
    void WindowFocusChanged(bool Gained);
    void ApplicationActiveChanged(bool Active);
    // Test hooks
    ScreenSaverInhibitor * ScreenSaver() const
    {
        return m_ScreenSaver;
    }
    void SaveWindowLocation();
    bool RomBrowserShown() const;
    // What SetWindowPos(hWnd) of a video plugin is on Windows: a plugin that has its own idea of
    // the size of the video area (GLideN64: its windowed resolution) asks for it with
    // QMetaObject::invokeMethod(window, "SetRenderSize", ...). Only while a game shows in the
    // window (not in the ROM browser, not in full screen).
    Q_INVOKABLE void SetRenderSize(int Width, int Height);
    // What a drop of files does (the drop event itself is Qt's)
    bool DropFiles(const QList<QUrl> & Urls);
    void ShowErrorDialog(const QString & title, const QString & message);
    void ShowWarningDialog(const QString & message);
    bool AskYesNo(const QString & question);
    void SetFullScreenRequested();
    // Test hooks of the status bar of a game
    QLabel * StateLabel() const
    {
        return m_StateLabel;
    }
    QLabel * SlotLabel() const
    {
        return m_SlotLabel;
    }
    QLabel * CpuLabel() const
    {
        return m_CpuLabel;
    }
    QLabel * VideoLabel() const
    {
        return m_VideoLabel;
    }
    QToolButton * PauseButton() const
    {
        return m_PauseButton;
    }
    QToolButton * FullscreenButton() const
    {
        return m_FullscreenButton;
    }

    // Opens and runs a ROM given by path directly, bypassing the Open ROM...
    // file dialog - used for `vortex64 <rom>` on the command line and
    // for packaging/linux/vortex64.desktop's `Exec=vortex64 %f`
    // (a file manager's "Open With" launches this way).
    void OpenRomPath(const QString & path);

    // Entry points used by QtMainMenu (CMainMenu's handlers)
    void OpenImage(const QString & path);
    void OpenComboImage(const QString & Rom, const QString & Disk);
    void ShowRomInfo();
    void ShowSettings();
    void ConfigPlugin(int Type);
    void ChooseRomDirectory();
    void RefreshRomList();
    // The Vortex64 library: further directories besides RomList_GameDir
    void AddLibraryDirectory(const QString & Directory);
    void AddLibraryDirectoryDialog();
    void SyncLibraryDirectories();
    void OpenDebugger();
    void OpenSettings(const QString & Page = QString());
    void MountDisk(const QString & Disk);
    Vortex::Shell * LibraryShell() const
    {
        return m_Shell;
    }
    void AddRecentDirectory(const QString & Directory);
    void ReloadRomBrowserColumns();
    void MakeWindowOnTop(bool OnTop);
    QtMainMenu * Menu() const
    {
        return m_Menu;
    }
    // The video area (test hook: its mouse events reach the scripts)
    QWidget * VideoWidget() const
    {
        return m_VideoWidget;
    }

private Q_SLOTS: // QT_NO_KEYWORDS is set (see CMakeLists.txt) - "slots" isn't available
    void OnSettings();
    void OnConfigPlugin(int Type);
    void OnChooseRomDirectory();
    void OnRefreshRomList();
    void ShowRomBrowser(bool Show);
    void AddRecentDir(const QString & Directory);

protected:
    void closeEvent(QCloseEvent * event) override;
    // Mouse of the video area for the scripts (WM_*BUTTON*, WM_MOUSEMOVE of upstream's WndProc)
    bool eventFilter(QObject * Watched, QEvent * Event) override;
    void changeEvent(QEvent * Event) override;
    void moveEvent(QMoveEvent * Event) override;
    void resizeEvent(QResizeEvent * Event) override;
    // WM_DROPFILES: a dropped ROM or disk image is run
    void dragEnterEvent(QDragEnterEvent * Event) override;
    void dropEvent(QDropEvent * Event) override;

private:
    enum EditKind
    {
        EditSettings,
        EditCheats,
        EditEnhancements
    };
    void EditRomSettings(const QString & Path, EditKind Kind);
    void RunImage(const QString & path, const std::function<bool()> & Start);
    QStackedWidget * m_Stack = nullptr;
    QtRomBrowser * m_RomBrowser = nullptr; // the scanner behind the library (CRomList); never shown
    Vortex::Shell * m_Shell = nullptr;
    QtMainMenu * m_Menu = nullptr;
    bool m_SawRunning = false;

    void AddRecentRom(const QString & path);
    void LoadRecentRoms();

    // The setting callbacks of CMainGui (they run in the thread that changes the setting)
    void RegisterSettingCallbacks();
    void UnregisterSettingCallbacks();
    void InGuiThread(std::function<void()> Function);
    void OnGameCpuRunning();
    void OnGamePaused();
    void OnLoadingInProgress();
    void OnGameLoaded();
    void OnShowStatusBar();
    void OnRomBrowserEnabled();
    void OnRomListChanged();
    void OnDiscordSetting();
    void OnScreenSaverSetting();
    void SetWindowCaption(const QString & Title);
    void BringToTop();
    void UpdateScreenSaver();
    // The window while the ROM browser shows (resizable, remembered position and size) and
    // while a game runs (a fixed 640x480 video area)
    void EnterBrowserMode();
    void EnterGameMode();
    void RestoreWindowPosition(bool Browser);

    QLabel * m_StatusLabel;
    QLabel * m_StatusLabel2 = nullptr;
    // the status bar of a running game: state, message, speed, CPU core, video plugin and two buttons
    QLabel * m_StateLabel = nullptr;
    QLabel * m_CpuLabel = nullptr;
    QLabel * m_SlotLabel = nullptr;
    QLabel * m_VideoLabel = nullptr;
    QToolButton * m_PauseButton = nullptr;
    QToolButton * m_FullscreenButton = nullptr;
    QTimer m_StatusTimer;
    void UpdateStatusInfo();
    // The status bar of a game fits its width: what does not fit goes in this order (see FitStatusBar)
    void FitStatusBar();
    int m_SpeedWidth = 0;
    struct StatusTexts
    {
        QString Message;
        QString SlotLong, SlotShort, CpuLong, CpuShort, VideoLong, VideoShort;
    } m_StatusTexts;
    void CreateShell();
    void OnLanguageChanged();
    void UpdateChrome();
    ScreenSaverInhibitor * m_ScreenSaver = nullptr;
    QTimer m_SavePositionTimer;
    bool m_SaveMainPosition = false;
    bool m_SaveBrowserPosition = false;
    QPoint m_MainPosition;
    QPoint m_BrowserPosition;
    bool m_CallbacksRegistered = false;
    bool m_DiscordActive = false;
    std::vector<std::pair<int, void *>> m_Registered; // SettingID and callback
    bool m_ModeApplied = false;
    QtNotification * m_Notify;
    QWidget * m_VideoWidget;
    bool m_RomRunning;
    bool m_PluginsConfigured;
    bool m_ShuttingDown;
    QSize m_RenderSize; // the size the video plugin asked for (windowed): back from full screen the window has it again
    QPoint m_LastMousePosition;
};
