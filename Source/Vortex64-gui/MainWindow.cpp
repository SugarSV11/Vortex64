#include "Debugger/DebuggerUI.h"
#include "MainWindow.h"
#include "MainMenu.h"
#include "QtNotification.h"
#include "ScreenSaverInhibit.h"
#include "PresentSync.h"
#include "RomBrowser.h"
#include "RomInformation.h"
#include "Vortex/Icons.h"
#include "Vortex/Shell.h"
#include "Vortex/Store.h"
#include "Vortex/InputBridge.h"
#include "Vortex/PluginDirCheck.h"
#include "Vortex/Language.h"
#include "Vortex/Strings.h"
#include "Vortex/Theme.h"
#include "Settings/SettingsDialog.h"
#include "Settings/VortexPages.h"
#include "Dialogs/CheatDialog.h"
#include "Dialogs/EnhancementDialog.h"
#include <Common/IniFile.h>
#include <Vortex64-core/N64System/N64Disk.h>
#include <Vortex64-core/Settings/SettingType/SettingsType-Application.h>
#include <Vortex64-ui/UserInterface/DiscordRPC.h>
#include <Vortex64-core/N64System/N64Rom.h>
#include <Vortex64-core/Settings/SettingsID.h>
#include <Vortex64-ui/Settings/UISettings.h>

#include <Common/path.h>
#include <Vortex64-core/N64System/N64System.h>
#include <Vortex64-core/N64System/SystemGlobals.h>
#include <Vortex64-core/Plugins/Plugin.h>
#include <Vortex64-core/Settings.h>
#include <algorithm>
#include <iterator>
#include <Vortex64-core/N64System/Enhancement/Enhancements.h>

#include <cstdint>
#include <functional>

#include <QAction>
#include <QCloseEvent>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <cstring>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QLayout>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMimeData>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QUrl>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QToolButton>
#include <QStatusBar>
#include <QTimer>
#include <QThread>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>

#include <X11/Xlib.h>

namespace
{
// Hands the video plugin this window's real native handle (see
// GetVideoWindowHandle()) so gfxSstWinOpen() (Vortex64-video/Renderer/
// OGLglitchmain.cpp) can embed into it via SDL_CreateWindowFrom() instead
// of opening its own top-level window - see Plugin.h's RenderWindow
// comment for why this method exists only on this (desktop Linux, non-
// Android) branch of the interface. m_MainWindow is set once, right after
// construction (see MainWindow's constructor) rather than passed in here,
// because this object is a file-static constructed at static-init time,
// before any MainWindow exists yet.
class CQtRenderWindow : public RenderWindow
{
public:
    void SetMainWindow(MainWindow * mainWindow)
    {
        m_MainWindow = mainWindow;
    }
    void * GetWindowHandle(void) const override
    {
        return m_MainWindow != nullptr ? m_MainWindow->GetVideoWindowHandle() : nullptr;
    }

private:
    MainWindow * m_MainWindow = nullptr;
};
} // namespace

static CQtRenderWindow s_MainRenderWindow;

MainWindow::MainWindow(QWidget * parent) :
    QMainWindow(parent),
    m_StatusLabel(nullptr),
    m_Notify(nullptr),
    m_VideoWidget(nullptr),
    m_RomRunning(false),
    m_PluginsConfigured(false),
    m_ShuttingDown(false)
{
    setWindowTitle(QStringLiteral("Vortex64"));
    resize(640, 480);
    new Vortex::InputBridge(this); // the keyboard and the mouse of a running game (a child of the window)

    // The two panes of CMainGui::CreateStatusBar (Resize: the second is 135 pixels wide)
    m_StateLabel = new QLabel(this);
    m_StateLabel->setObjectName("VxStatusState");
    m_StatusLabel = new QLabel(this);
    m_StatusLabel->setObjectName("VxStatusMessage");
    // the message gives way to the permanent panes (slot, CPU, video) when the 640 px of a game window are not enough for all
    m_StatusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_StatusLabel2 = new QLabel(this);
    m_StatusLabel2->setObjectName("VxStatusSpeed");
    m_CpuLabel = new QLabel(this);
    m_CpuLabel->setObjectName("VxStatusCpu");
    m_VideoLabel = new QLabel(this);
    m_VideoLabel->setObjectName("VxStatusVideo");
    m_SlotLabel = new QLabel(this);
    m_SlotLabel->setObjectName("VxStatusSlot");
    m_SlotLabel->setToolTip(Vortex::Tr("Ranura de los estados guardados (Emulación → Ranura de estado)", "Slot of the saved states (Emulation → State slot)"));
    m_PauseButton = new QToolButton(this);
    m_PauseButton->setObjectName("VxStatusPause");
    m_PauseButton->setProperty("vx", "icon");
    m_PauseButton->setAutoRaise(true);
    m_FullscreenButton = new QToolButton(this);
    m_FullscreenButton->setObjectName("VxStatusFullscreen");
    m_FullscreenButton->setProperty("vx", "icon");
    m_FullscreenButton->setAutoRaise(true);
    m_FullscreenButton->setIcon(Vortex::MakeIcon(Vortex::Icon::Fullscreen, 16));
    m_FullscreenButton->setToolTip(Vortex::Tr("Pantalla completa", "Full screen"));
    m_PauseButton->setIcon(Vortex::MakeIcon(Vortex::Icon::Pause, 16));
    m_PauseButton->setToolTip(Vortex::Tr("Pausar / reanudar", "Pause / resume"));
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(m_StateLabel);
    statusBar()->addWidget(m_StatusLabel, 1);
    statusBar()->addPermanentWidget(m_StatusLabel2);
    statusBar()->addPermanentWidget(m_SlotLabel);
    statusBar()->addPermanentWidget(m_CpuLabel);
    statusBar()->addPermanentWidget(m_VideoLabel);
    statusBar()->addPermanentWidget(m_PauseButton);
    statusBar()->addPermanentWidget(m_FullscreenButton);
    connect(m_PauseButton, &QToolButton::clicked, this, [this]() {
        if (m_Menu != nullptr)
        {
            m_Menu->ProcessMessage(ID_SYSTEM_PAUSE);
        }
    });
    connect(m_FullscreenButton, &QToolButton::clicked, this, [this]() {
        if (m_Menu != nullptr)
        {
            m_Menu->ProcessMessage(ID_OPTIONS_FULLSCREEN);
        }
    });
    m_StatusTimer.setInterval(1000);
    connect(&m_StatusTimer, &QTimer::timeout, this, &MainWindow::UpdateStatusInfo);
    m_StatusTimer.start();
    setAcceptDrops(true);
    m_ScreenSaver = new ScreenSaverInhibitor(this);
    new Vortex::PresentSync(this); // the monitor of the window and how the video plugin presents on it (a child of the window)
    // the location is saved a second after the window stopped moving (Timer_SetWindowPos)
    m_SavePositionTimer.setSingleShot(true);
    m_SavePositionTimer.setInterval(1000);
    connect(&m_SavePositionTimer, &QTimer::timeout, this, &MainWindow::SaveWindowLocation);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState State) { ApplicationActiveChanged(State == Qt::ApplicationActive); });

    // The menus are built by CreateLanguageMenus() (they need the language
    // files and the UI settings, which exist only after AppInit()).

    // Central widget the video plugin embeds its GL rendering into (see
    // GetVideoWindowHandle()/gfxSstWinOpen()'s SDL_CreateWindowFrom() path)
    // instead of opening its own top-level SDL2 window. WA_NativeWindow
    // makes GetVideoWindowHandle()'s winId() call return a real native
    // (X11) window rather than a lightweight Qt-only surface.
    // WA_NoSystemBackground/WA_PaintOnScreen tell Qt not to paint/clear
    // this widget itself, since SDL/GL owns everything drawn into it once
    // embedded. Deliberately NOT calling winId() here to force early
    // native-window creation: at construction time this widget hasn't
    // been through a real layout pass yet (the menu bar's height isn't
    // reserved until then), so its native window would end up created at
    // the wrong geometry - one that then visually covers the menu bar,
    // since Qt doesn't reliably keep repositioning a foreign-embedded
    // native window through later layout passes the way it does for
    // normal (Qt-painted) child widgets. GetVideoWindowHandle() calling
    // winId() lazily - by which point main.cpp has already shown the
    // window and let the event loop run at least once (see its own
    // comment) - gets the correct, already-laid-out geometry instead.
    m_VideoWidget = new QWidget(this);
    m_VideoWidget->setAttribute(Qt::WA_NativeWindow);
    m_VideoWidget->setAttribute(Qt::WA_NoSystemBackground);
    m_VideoWidget->setAttribute(Qt::WA_PaintOnScreen);
    m_VideoWidget->setAttribute(Qt::WA_OpaquePaintEvent);
    m_VideoWidget->setMouseTracking(true);
    m_VideoWidget->installEventFilter(this);
    // Upstream shows the ROM browser in the main window while no game runs and
    // the video output while one does; the stack switches between them.
    m_Stack = new QStackedWidget(this);
    m_Stack->addWidget(m_VideoWidget);
    setCentralWidget(m_Stack);

    // Built after the menu/statusbar/video widget exist since
    // QtNotification's doDisplayMessage()/doDisplayError() touch the
    // former, and s_MainRenderWindow.GetWindowHandle() (see this file's
    // anonymous namespace above) reads the latter - both only valid once
    // `this` is otherwise fully constructed and safely referenceable (see
    // QtNotification.h's own header comment on the same requirement).
    m_Notify = new QtNotification(this);
    s_MainRenderWindow.SetMainWindow(this);
}

MainWindow::~MainWindow()
{
    UnregisterSettingCallbacks();
    s_MainRenderWindow.SetMainWindow(nullptr);
}

void MainWindow::closeEvent(QCloseEvent * event)
{
    if (m_ShuttingDown)
    {
        event->ignore();
        return;
    }
    // "Confirm on exit" of the settings: only with a game running
    if (m_RomRunning && g_Settings != nullptr && g_Settings->LoadBool(GameRunning_CPU_Running) && Vortex::Store::Instance().Value("confirmExit", false).toBool())
    {
        if (!AskYesNo(Vortex::Tr("Hay un juego en marcha. ¿Salir de Vortex64?", "A game is running. Exit Vortex64?")))
        {
            event->ignore();
            return;
        }
    }
    m_ShuttingDown = true;
    // WM_DESTROY: the window location is saved, the screen saver is given back
    UnregisterSettingCallbacks();
    SaveWindowLocation();
    m_ScreenSaver->Set(false);
    if (g_BaseSystem != nullptr)
    {
        DebuggerReleaseStep();
        CN64System::CloseSystem();
    }
    m_RomRunning = false;
    s_MainRenderWindow.SetMainWindow(nullptr);
    // CRomList's destructor uses the settings, which main() destroys (AppCleanup)
    // before this window itself goes away
    if (m_Shell != nullptr)
    {
        m_Stack->removeWidget(m_Shell);
        delete m_Shell;
        m_Shell = nullptr;
    }
    Vortex::Store::Instance().SessionEnd();
    Vortex::Store::Instance().Flush();
    if (m_RomBrowser != nullptr)
    {
        delete m_RomBrowser;
        m_RomBrowser = nullptr;
    }
    event->accept();
}

bool MainWindow::eventFilter(QObject * Watched, QEvent * Event)
{
    if (Watched == m_VideoWidget && g_Debugger != nullptr && g_DebugSettings.haveDebugger)
    {
        const QEvent::Type Type = Event->type();
        if (Type == QEvent::MouseButtonPress || Type == QEvent::MouseButtonRelease || Type == QEvent::MouseMove)
        {
            QMouseEvent * Mouse = static_cast<QMouseEvent *>(Event);
            const QPoint Position = Mouse->position().toPoint();
            if (Type == QEvent::MouseMove)
            {
                // upstream reports a move only when the position changed
                if (Position != m_LastMousePosition)
                {
                    m_LastMousePosition = Position;
                    static_cast<CDebuggerUI *>(g_Debugger)->ScriptMouseEvent(2, Position.x(), Position.y(), 0);
                }
            }
            else
            {
                const Qt::MouseButton Button = Mouse->button();
                const int Index = Button == Qt::LeftButton ? 0 : Button == Qt::MiddleButton ? 1 : Button == Qt::RightButton ? 2 : -1;
                if (Index >= 0)
                {
                    static_cast<CDebuggerUI *>(g_Debugger)->ScriptMouseEvent(Type == QEvent::MouseButtonPress ? 0 : 1, Position.x(), Position.y(), Index);
                }
            }
        }
    }
    return QMainWindow::eventFilter(Watched, Event);
}

CNotification * MainWindow::GetNotify() const
{
    return m_Notify;
}

void * MainWindow::GetVideoWindowHandle() const
{
    if (QThread::currentThread() != thread())
    {
        void * handle = nullptr;
        QMetaObject::invokeMethod(const_cast<MainWindow *>(this), [this, &handle]()
                                  { handle = GetVideoWindowHandle(); },
                                  Qt::BlockingQueuedConnection);
        return handle;
    }
    if (QGuiApplication::platformName() != QStringLiteral("xcb") || m_VideoWidget == nullptr)
    {
        return nullptr;
    }

    // m_VideoWidget->winId() (an X11 Window XID under the x11 platform
    // plugin) reaches gfxSstWinOpen()'s SDL_CreateWindowFrom() path
    // correctly once handed off - RenderWindow::GetWindowHandle(),
    // GFXPlugin.cpp populating GFX_INFO::hWnd, and
    // SDL_CreateWindowFrom()+fallback in OGLglitchmain.cpp all already
    // work. The one real gap was that Qt's XCB connection and SDL2's own
    // separate Xlib connection aren't otherwise synchronized: SDL can call
    // XGetWindowAttributes() on this XID (from CN64System's emulation
    // thread) before the X server has actually processed Qt's
    // window-creation request on Qt's own connection, producing a fatal
    // BadWindow X error (Xlib's default error handler aborts the process -
    // not something gfxSstWinOpen()'s own null/failure-return fallback can
    // catch, since it isn't an SDL API failure).
    //
    // Fixed by reaching into Qt's own Xlib Display* via its native
    // interface (QNativeInterface::QX11Application, xcb platform plugin
    // only - see qguiapplication_platform.h) and calling XSync() on it
    // ourselves before returning the handle, forcing every pending
    // request Qt made about this window (including its creation) to be
    // flushed to and acknowledged by the X server first. SDL then opens
    // its own, separate Xlib connection (Xlib guarantees whole-server
    // request ordering across connections, so a later connection always
    // sees an earlier connection's already-server-acknowledged state) and
    // XGetWindowAttributes() on this XID is safe.
    QWindow * nativeWindow = m_VideoWidget->windowHandle();
    if (nativeWindow != nullptr)
    {
        nativeWindow->create();
    }
    const WId id = m_VideoWidget->winId();
    if (id == 0)
    {
        return nullptr;
    }
    if (auto * x11App = qGuiApp->nativeInterface<QNativeInterface::QX11Application>())
    {
        Display * display = x11App->display();
        if (display != nullptr)
        {
            XSync(display, False);
        }
    }
    return reinterpret_cast<void *>(id);
}

// CMainGui::SetStatusText: a message stays until the next one replaces it
void MainWindow::ShowStatusMessage(const QString & message, int /*timeoutMs*/)
{
    m_StatusTexts.Message = message;
    FitStatusBar();
}

void MainWindow::ShowStatusMessage2(const QString & message)
{
    m_StatusLabel2->setText(message);
    FitStatusBar();
}

void MainWindow::ShowErrorDialog(const QString & title, const QString & message)
{
    LeaveFullScreen();
    QMessageBox::critical(this, title, message);
}

void MainWindow::ShowWarningDialog(const QString & message)
{
    LeaveFullScreen();
    QMessageBox::warning(this, Vortex::Tr("Aviso", "Warning"), message);
}

bool MainWindow::AskYesNo(const QString & question)
{
    LeaveFullScreen();
    return QMessageBox::question(this, QStringLiteral("Vortex64"), question, QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
}

void MainWindow::SetFullScreenRequested()
{
    UISettingsSaveBool(UserInterface_InFullScreen, !isFullScreen());
    if (isFullScreen())
    {
        // (upstream ShowCursor) the pointer is back
        unsetCursor();
        m_VideoWidget->unsetCursor();
        showNormal();
        // the window of a running game is fixed again, the one of the browser is not
        if (g_Settings->LoadBool(GameRunning_CPU_Running))
        {
            EnterGameMode();
            if (m_RenderSize.isValid())
            {
                SetRenderSize(m_RenderSize.width(), m_RenderSize.height());
            }
        }
        else
        {
            EnterBrowserMode();
        }
    }
    else
    {
        // the video area follows the screen
        layout()->setSizeConstraint(QLayout::SetDefaultConstraint);
        m_Stack->setMinimumSize(0, 0);
        m_Stack->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        menuBar()->hide();
        statusBar()->hide();
        // upstream hides the pointer in full screen (ShowCursor(false))
        setCursor(Qt::BlankCursor);
        m_VideoWidget->setCursor(Qt::BlankCursor);
        showFullScreen();
    }
}

// CNotificationImp::WindowMode: a dialog must not open behind the full screen window; the
// change of mode gets a moment to finish (upstream waits 5 x 50 ms)
void MainWindow::LeaveFullScreen()
{
    if (!isFullScreen())
    {
        return;
    }
    SetFullScreenRequested();
    QElapsedTimer Wait;
    Wait.start();
    while (Wait.elapsed() < 250 && isFullScreen())
    {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
    }
}

void MainWindow::OpenRomPath(const QString & path)
{
    OpenImage(path);
}

void MainWindow::LoadRecentRoms()
{
    // The MRU list lives in the UI settings (File_RecentGameFileIndex/
    // File_RecentGameFileCount), exactly as in Project64/UserInterface/
    // MainWindow.cpp, so the Settings dialog's "Max # of Roms Remembered"
    // controls it. Nothing to preload here.
}

void MainWindow::AddRecentRom(const QString & path)
{
    // CMainGui::AddRecentRom: most-recently-run first, no duplicates
    // (case-insensitive), capped at File_RecentGameFileCount.
    const uint32_t MaxRememberedFiles = UISettingsLoadDword(File_RecentGameFileCount);
    QStringList RecentGames;
    for (uint32_t i = 0; i < MaxRememberedFiles; i++)
    {
        const std::string RecentGame = UISettingsLoadStringIndex(File_RecentGameFileIndex, (int32_t)i);
        if (RecentGame.empty())
        {
            break;
        }
        RecentGames.push_back(QString::fromStdString(RecentGame));
    }
    for (int i = 0; i < RecentGames.size(); i++)
    {
        if (RecentGames[i].compare(path, Qt::CaseInsensitive) == 0)
        {
            RecentGames.removeAt(i);
            break;
        }
    }
    RecentGames.prepend(path);
    while ((uint32_t)RecentGames.size() > MaxRememberedFiles)
    {
        RecentGames.removeLast();
    }
    for (int i = 0; i < RecentGames.size(); i++)
    {
        UISettingsSaveStringIndex(File_RecentGameFileIndex, i, RecentGames[i].toStdString());
    }
}

void MainWindow::RunImage(const QString & path, const std::function<bool()> & Start)
{
    if (m_RomRunning && g_BaseSystem != nullptr)
    {
        DebuggerReleaseStep();
        g_BaseSystem->CloseCpu();
    }

    if (!m_PluginsConfigured)
    {
        InitPlugins();
        if (g_Plugins->Gfx() == nullptr)
        {
            ShowErrorDialog(tr("Error"), tr("Video plugin failed to load."));
            return;
        }
    }

    // The browser goes away when the game runs and comes back when loading fails or the game ends
    // (OnGameCpuRunning, OnLoadingInProgress); the recent ROM list and the caption follow
    // Game_File (OnGameLoaded)
    m_RomRunning = Start();
    // "Iniciar" starts the game: Setting_AutoStart off (the ROM waits for File > Start emulation of the classic menu) does not
    // apply to Vortex64, which has no such step
    if (m_RomRunning && g_BaseSystem != nullptr && !g_Settings->LoadBool(Setting_AutoStart) && !g_Settings->LoadBool(GameRunning_CPU_Running))
    {
        g_BaseSystem->StartEmulation(true);
    }
}

// CMainMenu::OnOpenRom / recent ROM entries: disk images run as 64DD disks
void MainWindow::OpenImage(const QString & path)
{
    const QString Ext = QFileInfo(path).suffix().toLower();
    if (Ext == "ndd" || Ext == "d64")
    {
        RunImage(path, [&path]() { return CN64System::RunDiskImage(path.toUtf8().constData()); });
    }
    else
    {
        RunImage(path, [&path]() { return CN64System::RunFileImage(path.toUtf8().constData()); });
    }
}

void MainWindow::OpenComboImage(const QString & Rom, const QString & Disk)
{
    RunImage(Rom, [&Rom, &Disk]() { return CN64System::RunDiskComboImage(Rom.toUtf8().constData(), Disk.toUtf8().constData()); });
}

void MainWindow::ShowRomInfo()
{
    ShowRunningRomInformation(this);
}

void MainWindow::ShowSettings()
{
    OnSettings();
}

void MainWindow::ConfigPlugin(int Type)
{
    OnConfigPlugin(Type);
}

void MainWindow::ChooseRomDirectory()
{
    OnChooseRomDirectory();
}

void MainWindow::RefreshRomList()
{
    OnRefreshRomList();
}

void MainWindow::AddRecentDirectory(const QString & Directory)
{
    AddRecentDir(Directory);
}

void MainWindow::ReloadRomBrowserColumns()
{
    if (m_RomBrowser != nullptr)
    {
        m_RomBrowser->ReloadColumns();
    }
}

// ID_OPTIONS_ALWAYSONTOP: _NET_WM_STATE_ABOVE through the window manager
// (changing Qt's window flags would recreate the native window the video
// plugin is embedded in).
void MainWindow::MakeWindowOnTop(bool OnTop)
{
    Display * Dpy = nullptr;
    if (auto * x11App = qGuiApp->nativeInterface<QNativeInterface::QX11Application>())
    {
        Dpy = x11App->display();
    }
    if (Dpy == nullptr)
    {
        return;
    }
    XEvent Event = {};
    Event.xclient.type = ClientMessage;
    Event.xclient.window = (Window)winId();
    Event.xclient.message_type = XInternAtom(Dpy, "_NET_WM_STATE", False);
    Event.xclient.format = 32;
    Event.xclient.data.l[0] = OnTop ? 1 : 0; // _NET_WM_STATE_ADD / _REMOVE
    Event.xclient.data.l[1] = (long)XInternAtom(Dpy, "_NET_WM_STATE_ABOVE", False);
    Event.xclient.data.l[3] = 1;
    XSendEvent(Dpy, DefaultRootWindow(Dpy), False, SubstructureRedirectMask | SubstructureNotifyMask, &Event);
    XFlush(Dpy);
}

void MainWindow::OnSettings()
{
    OpenSettings();
}

// ID_OPTIONS_CONFIG_GFX/AUDIO/RSP/CONT: CPlugins::ConfigPlugin() pauses the
// emulation and calls the plugin's DllConfig().
void MainWindow::OnConfigPlugin(int Type)
{
    if (g_Plugins == nullptr)
    {
        return;
    }
    g_Plugins->ConfigPlugin(reinterpret_cast<void *>(winId()), (PLUGIN_TYPE)Type);
}

// Menus whose text comes from the language files can only be created once
// AppInit() has loaded the language (g_Lang), which happens after this
// window is constructed (AppInit needs the window's notification object).
void MainWindow::CreateLanguageMenus()
{
    // the core's language file is the one of Vortex64's language, and follows it from now on
    Vortex::SyncCoreLanguage(true);
    connect(&Vortex::LanguageNotifier::Instance(), &Vortex::LanguageNotifier::Changed, this, &MainWindow::OnLanguageChanged);
    // ROM browser (RomBrowser_Enabled) and its File menu entries.
    m_RomBrowser = new QtRomBrowser(this);
    m_RomBrowser->hide(); // CRomList does the scanning, the cache and the watch; Vortex::Shell presents the list
    connect(m_RomBrowser, &QtRomBrowser::RomActivated, this, [this](const QString & Path) { OpenImage(Path); });
    connect(m_RomBrowser, &QtRomBrowser::RomInfoRequested, this, [this](const QString & Path) { ShowRomInformation(this, Path); });
    // The pop-up menu of the ROM browser (WM_COMMAND ID_POPUPMENU_* of upstream's MainGui_Proc)
    connect(m_RomBrowser, &QtRomBrowser::EditGameSettingsRequested, this, [this](const QString & Path) { EditRomSettings(Path, EditSettings); });
    connect(m_RomBrowser, &QtRomBrowser::EditCheatsRequested, this, [this](const QString & Path) { EditRomSettings(Path, EditCheats); });
    connect(m_RomBrowser, &QtRomBrowser::ChooseEnhancementRequested, this, [this](const QString & Path) { EditRomSettings(Path, EditEnhancements); });
    connect(m_RomBrowser, &QtRomBrowser::PlayWithDiskRequested, this, [this](const QString & Path) {
        const QString Start = QString::fromStdString(g_Settings->LoadStringVal(RomList_GameDir));
        const QString Disk = QFileDialog::getOpenFileName(this, QString(), Start, Vortex::Tr("Imagen de disco N64DD (*.ndd *.d64);;Todos los archivos (*)", "N64DD disk image (*.ndd *.d64);;All files (*)"));
        if (!Disk.isEmpty())
        {
            OpenComboImage(Path, Disk);
        }
    });
    connect(m_RomBrowser, &QtRomBrowser::RefreshRequested, this, &MainWindow::OnRefreshRomList);
    connect(m_RomBrowser, &QtRomBrowser::ChooseDirectoryRequested, this, &MainWindow::OnChooseRomDirectory);

    CreateShell();
    connect(&Vortex::Store::Instance(), &Vortex::Store::DirectoriesChanged, this, [this]() { SyncLibraryDirectories(); OnRefreshRomList(); });
    // the shell presents what the scanner has; the scanner starts from the cache (the games are there at once)
    SyncLibraryDirectories();
    m_Shell->SetSource(m_RomBrowser);
    m_RomBrowser->LoadRomList();

    // The whole menu bar (File, System, Options, Debugger, Help) is the port of
    // CMainMenu; it rebuilds itself when the state its items depend on changes.
    m_Menu = new QtMainMenu(this);

    // what CMainGui does when it is created: the settings it follows, the status bar setting, the
    // place of the window, the browser
    RegisterSettingCallbacks();
    ShowRomBrowser(true);
    if (UISettingsLoadBool(Setting_EnableDiscordRPC))
    {
        CDiscord::Init();
        m_DiscordActive = true;
        CDiscord::Update(false);
    }
}

// The shell of the library (sidebar, pages, detail strip) with its connections. It is built once and again when the
// language changes (the texts are given when the widgets are built); the page, filter, view mode and selection come back
// from the store.
void MainWindow::CreateShell()
{
    m_Shell = new Vortex::Shell(nullptr, this);
    m_Stack->addWidget(m_Shell);
    connect(m_Shell, &Vortex::Shell::PlayRequested, this, [this](const QString & Path) { m_RomBrowser->StopRefresh(); OpenImage(Path); });
    connect(m_Shell, &Vortex::Shell::PlayWithDiskRequested, this, [this](const QString & Path) {
        const QString Start = QString::fromStdString(g_Settings->LoadStringVal(RomList_GameDir));
        const QString Disk = QFileDialog::getOpenFileName(this, QString(), Start, Vortex::Tr("Imagen de disco N64DD (*.ndd *.d64);;Todos los archivos (*)", "N64DD disk image (*.ndd *.d64);;All files (*)"));
        if (!Disk.isEmpty())
        {
            OpenComboImage(Path, Disk);
        }
    });
    connect(m_Shell, &Vortex::Shell::RomInfoRequested, this, [this](const QString & Path) { ShowRomInformation(this, Path); });
    connect(m_Shell, &Vortex::Shell::EditSettingsRequested, this, [this](const QString & Path) { EditRomSettings(Path, EditSettings); });
    connect(m_Shell, &Vortex::Shell::EditCheatsRequested, this, [this](const QString & Path) { EditRomSettings(Path, EditCheats); });
    connect(m_Shell, &Vortex::Shell::EditEnhancementsRequested, this, [this](const QString & Path) { EditRomSettings(Path, EditEnhancements); });
    connect(m_Shell, &Vortex::Shell::MountDiskRequested, this, &MainWindow::MountDisk);
    connect(m_Shell, &Vortex::Shell::OpenRomRequested, this, [this]() { if (m_Menu != nullptr) m_Menu->ProcessMessage(ID_FILE_OPEN_ROM); });
    connect(m_Shell, &Vortex::Shell::SettingsRequested, this, [this]() { OpenSettings(); });
    connect(m_Shell, &Vortex::Shell::DiskSettingsRequested, this, [this]() { OpenSettings(QStringLiteral("disk")); });
    connect(m_Shell, &Vortex::Shell::DebuggerRequested, this, &MainWindow::OpenDebugger);
    connect(m_Shell, &Vortex::Shell::AboutRequested, this, [this]() { if (m_Menu != nullptr) m_Menu->ProcessMessage(ID_HELP_ABOUT); });
    connect(m_Shell, &Vortex::Shell::RefreshRequested, this, &MainWindow::OnRefreshRomList);
    connect(m_Shell, &Vortex::Shell::AddDirectoryRequested, this, &MainWindow::AddLibraryDirectoryDialog);
}

// The language of the interface changed (Vortex::ApplyLanguage): the core's texts follow, then what was built with the old
// texts is built again - the shell of the library, the menus, the status bar and the windows of the debugger. A running
// game and its emulation are not touched.
void MainWindow::OnLanguageChanged()
{
    // (the core's language file already follows: Vortex::ApplyLanguage did it before the signal)
    if (m_Shell != nullptr && m_RomBrowser != nullptr)
    {
        const bool Showing = m_Stack->currentWidget() == m_Shell;
        const QString Selected = m_Shell->SelectedRom();
        m_Stack->removeWidget(m_Shell);
        delete m_Shell;
        m_Shell = nullptr;
        CreateShell();
        m_Shell->SetSource(m_RomBrowser);
        if (!Selected.isEmpty())
        {
            m_Shell->SelectRom(Selected); // (the store keeps the last selection only after one was made)
        }
        if (Showing)
        {
            m_Stack->setCurrentWidget(m_Shell);
        }
    }
    if (m_Menu != nullptr)
    {
        m_Menu->ResetMenu();
    }
    UpdateStatusInfo();
    UpdateChrome();
    if (m_VideoLabel != nullptr)
    {
        m_SlotLabel->setToolTip(Vortex::Tr("Ranura de los estados guardados (Emulación → Ranura de estado)", "Slot of the saved states (Emulation → State slot)"));
        m_FullscreenButton->setToolTip(Vortex::Tr("Pantalla completa", "Full screen"));
        m_PauseButton->setToolTip(Vortex::Tr("Pausar / reanudar", "Pause / resume"));
    }
    if (g_Debugger != nullptr)
    {
        static_cast<CDebuggerUI *>(g_Debugger)->LanguageChanged();
    }
    if (!m_RomRunning)
    {
        setWindowTitle(QStringLiteral("Vortex64"));
    }
}

// Project64/main.cpp: after AppInit() the frontend gives the plugin manager its
// render windows and loads the selected plugins (CPlugins::CreatePlugins scans
// nothing itself: the plugin choice comes from the Plugin_*_Current settings
// edited in Settings > Plugin), so their configuration entries are available
// before a ROM is opened.
void MainWindow::InitPlugins()
{
    if (m_PluginsConfigured || g_Plugins == nullptr)
    {
        return;
    }
    // The settings can still point the plugin directory to a folder of another build (the "Use Selected" of the Plugin Directory is
    // kept in the file of settings for ever): the input plugin and the video plugin of an older version run without what this
    // version gives them, and nothing says it. It is said, and the plugins that come with the program can be used instead.
    if (g_Settings->LoadBool(Directory_PluginUseSelected))
    {
        const QString Selected = QString::fromStdString(g_Settings->LoadStringVal(Directory_PluginSelected));
        const QString Own = QString::fromStdString(g_Settings->LoadStringVal(Directory_PluginInitial));
        const QStringList Different = Vortex::PluginsDifferentFromOwn(Selected, Own);
        if (!Different.isEmpty())
        {
            fprintf(stderr, "[plugins] the plugin directory %s has plugins that are not the ones of this program (%s): %s\n", Selected.toUtf8().constData(), Own.toUtf8().constData(), Different.join(", ").toUtf8().constData());
            const QString Question = Vortex::Tr("La carpeta de plugins elegida en Ajustes contiene plugins distintos de los de este programa (%1), probablemente de otra versión. Con ellos la entrada (teclado, ratón) o el vídeo pueden fallar.\n\nCarpeta elegida:\n%2\n\nCarpeta de este programa:\n%3\n\n¿Usar los plugins de este programa?", "The plugin folder chosen in Settings has plugins that are not the ones of this program (%1), probably from another version. With them the input (keyboard, mouse) or the video may fail.\n\nChosen folder:\n%2\n\nFolder of this program:\n%3\n\nUse the plugins of this program?").arg(Different.join(", "), Selected, Own);
            // (an automated run - the offscreen platform, or VORTEX_PLUGIN_DIR_PROMPT=no - keeps the folder it was given and only says so;
            // VORTEX_PLUGIN_DIR_PROMPT=yes uses the program's own without asking)
            const QByteArray Policy = qgetenv("VORTEX_PLUGIN_DIR_PROMPT");
            const bool Ask = Policy.isEmpty() && QGuiApplication::platformName() != QStringLiteral("offscreen");
            if (Policy == "yes" || (Ask && AskYesNo(Question)))
            {
                g_Settings->SaveBool(Directory_PluginUseSelected, false);
            }
        }
    }
    g_Plugins->SetRenderWindows(&s_MainRenderWindow, &s_MainRenderWindow);
    g_Plugins->CreatePlugins();
    m_PluginsConfigured = true;
}

// The ROM browser is created once the UI settings exist (after AppInit).
void MainWindow::ShowRomBrowser(bool Show)
{
    if (m_RomBrowser == nullptr || m_Stack == nullptr)
    {
        return;
    }
    // (the library is the face of Vortex64: RomBrowser_Enabled of the classic browser does not apply)
    const bool Browser = Show && !g_Settings->LoadBool(GameRunning_CPU_Running);
    if (m_ModeApplied && Browser == RomBrowserShown())
    {
        return; // CRomBrowser::ShowRomList/HideRomList do nothing when it is already so
    }
    m_ModeApplied = true;
    m_Stack->setCurrentWidget(Browser ? static_cast<QWidget *>(m_Shell) : m_VideoWidget);
    if (Browser)
    {
        EnterBrowserMode();
        m_RomBrowser->ShowingBrowser();
        m_RomBrowser->WatchStart();
    }
    else
    {
        m_RomBrowser->WatchStop();
        EnterGameMode();
    }
}

bool MainWindow::RomBrowserShown() const
{
    return m_Shell != nullptr && m_Stack->currentWidget() == m_Shell;
}

// ID_POPUPMENU_EDITSETTINGS/EDITCHEATS/CHOOSEENHANCEMENT: the settings, cheats or enhancements of
// the ROM (or disk) selected in the browser: its identity is made the current one for the
// dialog, and the one of the running game is put back afterwards
void MainWindow::EditRomSettings(const QString & Path, EditKind Kind)
{
    const QString Extension = QFileInfo(Path).suffix().toLower();
    const std::string File = Path.toUtf8().constData();
    if (Extension != "ndd" && Extension != "d64")
    {
        CN64Rom Rom;
        Rom.LoadN64Image(File.c_str(), true);
        Rom.SaveRomSettingID(true);
        if (Kind == EditSettings)
        {
            SettingsDialog Dialog(true, this);
            Dialog.exec();
        }
        else if (Kind == EditCheats)
        {
            g_Enhancements->ResetActive(nullptr);
            g_Enhancements->LoadActive(nullptr, nullptr);
            ShowCheatDialog(this);
        }
        else
        {
            g_Enhancements->LoadActive(nullptr, nullptr);
            ShowEnhancementDialog(this);
        }
        if (g_Rom)
        {
            g_Rom->SaveRomSettingID(false);
        }
        else
        {
            Rom.ClearRomSettingID();
        }
        return;
    }
    CN64Disk Disk;
    Disk.LoadDiskImage(File.c_str());
    Disk.SaveDiskSettingID(true);
    if (Kind == EditSettings)
    {
        SettingsDialog Dialog(true, this);
        Dialog.exec();
    }
    else if (Kind == EditCheats)
    {
        ShowCheatDialog(this);
    }
    if (g_Disk)
    {
        g_Disk->SaveDiskSettingID(false);
    }
    else
    {
        Disk.ClearDiskSettingID();
    }
}

// File > Choose ROM Directory (CRomBrowser::SelectRomDir).
void MainWindow::OnChooseRomDirectory()
{
    const QString Start = QString::fromStdString(g_Settings->LoadStringVal(RomList_GameDir));
    const QString Dir = QFileDialog::getExistingDirectory(this, QString::fromUtf8(GS(SELECT_ROM_DIR)), Start);
    if (Dir.isEmpty())
    {
        return;
    }
    QString WithSlash = Dir;
    if (!WithSlash.endsWith('/'))
    {
        WithSlash += '/';
    }
    g_Settings->SaveString(RomList_GameDir, WithSlash.toStdString());
    AddRecentDir(WithSlash);
    OnRefreshRomList();
    if (RomBrowserShown())
    {
        m_RomBrowser->WatchStart();
    }
}

void MainWindow::OnRefreshRomList()
{
    if (m_RomBrowser != nullptr)
    {
        m_RomBrowser->RefreshRomList();
    }
}

// The directories of the library: RomList_GameDir of the core and the extra ones of the store
void MainWindow::SyncLibraryDirectories()
{
    if (m_RomBrowser == nullptr)
    {
        return;
    }
    std::vector<std::string> Extra;
    for (const QString & Directory : Vortex::Store::Instance().ExtraDirectories())
    {
        Extra.push_back(Directory.toStdString());
    }
    m_RomBrowser->SetExtraGameDirectories(Extra);
}

void MainWindow::AddLibraryDirectory(const QString & Directory)
{
    QString WithSlash = Directory;
    if (!WithSlash.endsWith('/'))
    {
        WithSlash += '/';
    }
    const QString Main = QString::fromStdString(g_Settings->LoadStringVal(RomList_GameDir));
    if (Main.isEmpty())
    {
        g_Settings->SaveString(RomList_GameDir, WithSlash.toStdString());
        AddRecentDir(WithSlash);
        OnRefreshRomList();
        if (RomBrowserShown())
        {
            m_RomBrowser->WatchStart();
        }
        return;
    }
    if (QDir(Main).canonicalPath() == QDir(WithSlash).canonicalPath())
    {
        return;
    }
    QStringList Extra = Vortex::Store::Instance().ExtraDirectories();
    for (const QString & Existing : Extra)
    {
        if (QDir(Existing).canonicalPath() == QDir(WithSlash).canonicalPath())
        {
            return;
        }
    }
    Extra << WithSlash;
    Vortex::Store::Instance().SetExtraDirectories(Extra); // (rescans through DirectoriesChanged)
    if (RomBrowserShown())
    {
        m_RomBrowser->WatchStart();
    }
}

void MainWindow::AddLibraryDirectoryDialog()
{
    const QString Start = QString::fromStdString(g_Settings->LoadStringVal(RomList_GameDir));
    const QString Dir = QFileDialog::getExistingDirectory(this, Vortex::Tr("Añadir carpeta de ROMs", "Add ROM folder"), Start);
    if (!Dir.isEmpty())
    {
        AddLibraryDirectory(Dir);
    }
}

// The 64DD disk of the library: swapped into the running game when it has a disk drive, otherwise run
void MainWindow::MountDisk(const QString & Disk)
{
    if (m_RomRunning && g_Disk != nullptr && g_Settings->LoadBool(GameRunning_CPU_Running))
    {
        g_Disk->SaveDiskImage();
        g_Disk->SwapDiskImage(Disk.toUtf8().constData());
        return;
    }
    OpenImage(Disk);
}

void MainWindow::OpenDebugger()
{
    if (m_Menu == nullptr)
    {
        return;
    }
    if (!g_DebugSettings.haveDebugger)
    {
        if (AskYesNo(Vortex::Tr("El depurador está desactivado. ¿Abrir los ajustes avanzados para activarlo?", "The debugger is off. Open the advanced settings to turn it on?")))
        {
            OpenSettings(QStringLiteral("advanced"));
        }
        return;
    }
    m_Menu->ProcessMessage(ID_DEBUGGER_BREAKPOINTS);
}

void MainWindow::OpenSettings(const QString & Page)
{
    QString Shown = Page;
    for (;;)
    {
        SettingsDialog Dialog(false, this);
        if (!Shown.isEmpty())
        {
            Dialog.ShowPage(Shown);
        }
        Dialog.exec();
        if (!Dialog.ReopenRequested())
        {
            break;
        }
        Shown = Dialog.CurrentPageId(); // the language changed with Apply: the window again, with the new texts
    }
}

// CMainGui::AddRecentDir: same MRU rules as AddRecentRom, kept in
// Directory_RecentGameDirIndex / Directory_RecentGameDirCount.
void MainWindow::AddRecentDir(const QString & Directory)
{
    const uint32_t MaxRemembered = UISettingsLoadDword(Directory_RecentGameDirCount);
    QStringList Dirs;
    for (uint32_t i = 0; i < MaxRemembered; i++)
    {
        const std::string Dir = UISettingsLoadStringIndex(Directory_RecentGameDirIndex, (int32_t)i);
        if (Dir.empty())
        {
            break;
        }
        Dirs.push_back(QString::fromStdString(Dir));
    }
    for (int i = 0; i < Dirs.size(); i++)
    {
        if (Dirs[i].compare(Directory, Qt::CaseInsensitive) == 0)
        {
            Dirs.removeAt(i);
            break;
        }
    }
    Dirs.prepend(Directory);
    while ((uint32_t)Dirs.size() > MaxRemembered)
    {
        Dirs.removeLast();
    }
    for (int i = 0; i < Dirs.size(); i++)
    {
        UISettingsSaveStringIndex(Directory_RecentGameDirIndex, i, Dirs[i].toStdString());
    }
}


// ---- the behaviour of CMainGui ----

void MainWindow::InGuiThread(std::function<void()> Function)
{
    if (QThread::currentThread() == thread())
    {
        Function();
    }
    else
    {
        QMetaObject::invokeMethod(this, std::move(Function), Qt::QueuedConnection);
    }
}


void MainWindow::RegisterSettingCallbacks()
{
    if (m_CallbacksRegistered || g_Settings == nullptr)
    {
        return;
    }
    m_CallbacksRegistered = true;
    // The setting callbacks of upstream's CMainGui (RegisterChangeCB in its constructor); they run
    // in the thread that changes the setting, which is the emulation thread for GameRunning_*
    auto Register = [this](SettingID Setting, void (*Function)(MainWindow *)) {
        g_Settings->RegisterChangeCB(Setting, this, (CSettings::SettingChangedFunc)Function);
        m_Registered.push_back({(int)Setting, (void *)Function});
    };
    Register(GameRunning_CPU_Running, [](MainWindow * Window) { Window->InGuiThread([Window]() { Window->OnGameCpuRunning(); }); });
    Register(GameRunning_CPU_Paused, [](MainWindow * Window) { Window->InGuiThread([Window]() { Window->OnGamePaused(); }); });
    Register(GameRunning_LoadingInProgress, [](MainWindow * Window) { Window->InGuiThread([Window]() { Window->OnLoadingInProgress(); }); });
    Register(Game_File, [](MainWindow * Window) { Window->InGuiThread([Window]() { Window->OnGameLoaded(); }); });
    Register((SettingID)UserInterface_ShowStatusBar, [](MainWindow * Window) { Window->InGuiThread([Window]() { Window->OnShowStatusBar(); }); });
    Register((SettingID)RomBrowser_Enabled, [](MainWindow * Window) { Window->InGuiThread([Window]() { Window->OnRomBrowserEnabled(); }); });
    // CMainGui::RomBrowserListChanged
    Register(RomList_GameDirRecursive, [](MainWindow * Window) { Window->InGuiThread([Window]() { Window->OnRomListChanged(); }); });
    Register(RomList_ShowFileExtensions, [](MainWindow * Window) { Window->InGuiThread([Window]() { Window->OnRomListChanged(); }); });
    Register((SettingID)Setting_EnableDiscordRPC, [](MainWindow * Window) { Window->InGuiThread([Window]() { Window->OnDiscordSetting(); }); });
    Register((SettingID)Setting_DisableScrSaver, [](MainWindow * Window) { Window->InGuiThread([Window]() { Window->OnScreenSaverSetting(); }); });
}

// CMainGui::DiscordRPCChanged (and what its constructor does when the setting is on)
void MainWindow::OnDiscordSetting()
{
    if (UISettingsLoadBool(Setting_EnableDiscordRPC))
    {
        if (!m_DiscordActive)
        {
            CDiscord::Init();
            m_DiscordActive = true;
        }
        CDiscord::Update();
    }
    else if (m_DiscordActive)
    {
        CDiscord::Shutdown();
        m_DiscordActive = false;
    }
}

void MainWindow::UnregisterSettingCallbacks()
{
    if (!m_CallbacksRegistered || g_Settings == nullptr)
    {
        return;
    }
    m_CallbacksRegistered = false;
    for (const auto & Entry : m_Registered)
    {
        g_Settings->UnregisterChangeCB((SettingID)Entry.first, this, (CSettings::SettingChangedFunc)Entry.second);
    }
    m_Registered.clear();
    if (m_DiscordActive)
    {
        CDiscord::Shutdown();
        m_DiscordActive = false;
    }
}

// CMainGui::GameCpuRunning
void MainWindow::OnGameCpuRunning()
{
    if (m_ShuttingDown)
    {
        return;
    }
    if (g_Settings->LoadBool(GameRunning_CPU_Running))
    {
        MakeWindowOnTop(UISettingsLoadBool(UserInterface_AlwaysOnTop));
        ShowRomBrowser(false);
        Vortex::Store::Instance().SessionStart(QString::fromStdString(g_Settings->LoadStringVal(Game_File)));
        if (UISettingsLoadBool(Setting_AutoFullscreen))
        {
            // the ROM status decides, like "<status>.AutoFullScreen" of the ROM database
            CIniFile RomIniFile(g_Settings->LoadStringVal(SupportFile_RomDatabase).c_str());
            const std::string RomStatus = UISettingsLoadStringVal(Rdb_Status);
            char String[100];
            RomIniFile.GetString("ROM Status", stdstr_f("%s.AutoFullScreen", RomStatus.c_str()).c_str(), "true", String, sizeof(String));
            if (strcasecmp(String, "true") == 0)
            {
                g_Notify->ChangeFullScreen();
            }
        }
        BringToTop();
    }
    else
    {
        // the cheats cannot be edited for a game that ended (upstream sends IDCANCEL to its cheat window)
        for (QDialog * Open : findChildren<QDialog *>())
        {
            if (Open->isVisible() && (Open->objectName() == "CheatDialog" || Open->objectName() == "EnhancementDialog"))
            {
                Open->reject();
            }
        }
        // WM_GAME_CLOSED
        Vortex::Store::Instance().SessionEnd();
        setWindowTitle(QStringLiteral("Vortex64"));
        LeaveFullScreen();
        ShowRomBrowser(true);
        MakeWindowOnTop(false);
        ShowStatusMessage(QString(), 0);
        ShowStatusMessage2(QString());
    }
    UpdateScreenSaver();
}

void MainWindow::OnGamePaused()
{
    Vortex::Store::Instance().SessionPaused(g_Settings->LoadBool(GameRunning_CPU_Paused));
    UpdateScreenSaver();
}

// CMainGui::LoadingInProgressChanged: a load that ends without a game brings the browser back
void MainWindow::OnLoadingInProgress()
{
    if (m_ShuttingDown)
    {
        return;
    }
    if (!g_Settings->LoadBool(GameRunning_LoadingInProgress) && g_Settings->LoadStringVal(Game_File).length() == 0)
    {
        LeaveFullScreen();
        ShowRomBrowser(true);
        MakeWindowOnTop(false);
    }
}

// CMainGui::GameLoaded
void MainWindow::OnGameLoaded()
{
    const std::string File = g_Settings->LoadStringVal(Game_File);
    if (File.empty())
    {
        return;
    }
    AddRecentRom(QString::fromStdString(File));
    SetWindowCaption(QString::fromStdString(g_Settings->LoadStringVal(Rdb_GoodName)));
    if (UISettingsLoadBool(Setting_EnableDiscordRPC))
    {
        CDiscord::Update();
    }
}

// CMainGui::SetWindowCaption: "<title> - <application name>"
void MainWindow::SetWindowCaption(const QString & Title)
{
    setWindowTitle(QString::fromUtf8("Vortex64 \u2014 %1").arg(Title));
}

// CMainGui::ShowStatusBarChanged
void MainWindow::OnShowStatusBar()
{
    UpdateChrome();
}

// The menu bar and the status bar belong to a running game: the library has neither, nor does the full screen
void MainWindow::UpdateChrome()
{
    if (m_Shell == nullptr)
    {
        return;
    }
    const bool Game = !RomBrowserShown() && !isFullScreen();
    menuBar()->setVisible(Game);
    statusBar()->setVisible(Game && g_Settings->LoadBool((SettingID)UserInterface_ShowStatusBar));
    UpdateStatusInfo();
    QTimer::singleShot(0, this, [this]() { FitStatusBar(); }); // (once the bar has its width)
}

// The state, the CPU core and the video plugin of the running game: nothing that is not known is shown
void MainWindow::UpdateStatusInfo()
{
    if (g_Settings == nullptr || m_ShuttingDown)
    {
        return;
    }
    const bool Running = g_Settings->LoadBool(GameRunning_CPU_Running);
    const bool Paused = g_Settings->LoadBool(GameRunning_CPU_Paused);
    const bool Loading = g_Settings->LoadBool(GameRunning_LoadingInProgress);
    QString State;
    QString Color = "#5fb97a";
    if (Loading)
    {
        State = Vortex::Tr("Cargando", "Loading");
        Color = "#e0b04a";
    }
    else if (Running && Paused)
    {
        State = Vortex::Tr("En pausa", "Paused");
        Color = "#e0b04a";
    }
    else if (Running)
    {
        State = Vortex::Tr("Emulando", "Running");
    }
    else
    {
        State = Vortex::Tr("Detenido", "Stopped");
        Color = "#9aa0a9";
    }
    m_StateLabel->setText(QString("<span style='color:%1'>●</span>&nbsp;%2").arg(Color, State));
    QString Cpu;
    if (Running)
    {
        switch (g_Settings->LoadDword(Game_CpuType))
        {
        case CPU_Interpreter: Cpu = Vortex::Tr("Intérprete", "Interpreter"); break;
        case CPU_Recompiler: Cpu = QStringLiteral("JIT"); break;
        case CPU_SyncCores: Cpu = Vortex::Tr("Núcleos sincronizados", "Synchronised cores"); break;
        default: break;
        }
    }
    m_StatusTexts = StatusTexts{m_StatusTexts.Message, QString(), QString(), QString(), QString(), QString(), QString()};
    if (Running)
    {
        const uint32_t Slot = g_Settings->LoadDword(Game_CurrentSaveState);
        m_StatusTexts.SlotLong = Vortex::Tr("Ranura: ", "Slot: ") + (Slot == 0 ? Vortex::Tr("por defecto", "default") : QString::number(Slot));
        m_StatusTexts.SlotShort = Vortex::Tr("Ranura ", "Slot ") + QString::number(Slot);
    }
    if (!Cpu.isEmpty())
    {
        m_StatusTexts.CpuLong = QStringLiteral("CPU: ") + Cpu;
        m_StatusTexts.CpuShort = Cpu;
    }
    if (Running && g_Plugins != nullptr && g_Plugins->Gfx() != nullptr)
    {
        // the plugin as the rows of the settings name it (no version of a development build, no word "plugin")
        QString Video = Vortex::PluginDisplayName(QString::fromUtf8(g_Plugins->Gfx()->PluginName()));
        const int Colon = Video.indexOf(QLatin1Char(':'));
        if (Colon > 0)
        {
            Video = Video.left(Colon);
        }
        if (!Video.isEmpty())
        {
            m_StatusTexts.VideoLong = Vortex::Tr("Vídeo: ", "Video: ") + Video;
            m_StatusTexts.VideoShort = Video;
        }
    }
    m_PauseButton->setEnabled(Running);
    m_PauseButton->setIcon(Vortex::MakeIcon(Running && Paused ? Vortex::Icon::Play : Vortex::Icon::Pause, 16));
    m_FullscreenButton->setEnabled(Running);
    FitStatusBar();
}

// What the status bar of a game shows, by priority. Always: the state, the speed and the two buttons. The rest goes, from the first to give way:
//   1. the prefixes ("Ranura: por defecto" -> "Ranura 0", "CPU: JIT" -> "JIT", "Vídeo: GLideN64" -> "GLideN64")
//   2. the slot, 3. the CPU core, 4. the message (the status bar of a window of 640 px with the longest names), 5. the plugin
// The message takes the width that is left, cut with "..." (the whole text is in its tooltip); the widgets that are hidden keep their text in the tooltip of the
// state. Widths are those of the fonts of the moment (HiDPI, text scale, language), never fixed numbers.
void MainWindow::FitStatusBar()
{
    if (m_StateLabel == nullptr || m_StatusLabel == nullptr)
    {
        return;
    }
    if (!statusBar()->isVisible() || statusBar()->width() <= 0)
    {
        m_StatusLabel->setText(m_StatusTexts.Message); // (laid out when it shows: resizeEvent)
        return;
    }
    struct Level
    {
        bool Short, Slot, Cpu, Message, Video;
    };
    static const Level Levels[] = {
        {false, true, true, true, true}, {true, true, true, true, true},  {true, false, true, true, true},
        {true, false, false, true, true}, {true, false, false, false, true}, {true, false, false, false, false},
    };
    const QFontMetrics Metrics(m_StatusLabel->font());
    const int Gap = statusBar()->layout() != nullptr ? 2 * statusBar()->layout()->spacing() + 8 : 14;
    // a message gets a place when at least this much of it is shown (a few words), or all of it when it is short
    const int MessageMinimum = std::min(Metrics.horizontalAdvance(m_StatusTexts.Message) + Gap, Metrics.horizontalAdvance(QStringLiteral("Wwwwwwwwwwww")) + Gap);
    auto Width = [&](const QString & Text) { return Text.isEmpty() ? 0 : Metrics.horizontalAdvance(Text) + Gap; };
    // the widgets that never go: their size is the one of the moment (the speed only has a place when there is one; the width that its text had
    // is kept so that the other panes do not move every second)
    m_StatusLabel2->setVisible(!m_StatusLabel2->text().isEmpty());
    m_SpeedWidth = std::max(m_SpeedWidth, m_StatusLabel2->sizeHint().width());
    if (m_StatusLabel2->text().isEmpty())
    {
        m_SpeedWidth = 0;
    }
    m_StatusLabel2->setMinimumWidth(m_SpeedWidth);
    const int Fixed = m_StateLabel->sizeHint().width() + Gap + (m_SpeedWidth != 0 ? m_SpeedWidth + Gap : 0) +
                      m_PauseButton->sizeHint().width() + m_FullscreenButton->sizeHint().width() + 2 * Gap;
    const int Available = statusBar()->width() - 2 * statusBar()->contentsMargins().left();
    const Level * Chosen = &Levels[std::size(Levels) - 1];
    for (const Level & L : Levels)
    {
        const int Needed = Fixed + (L.Slot ? Width(L.Short ? m_StatusTexts.SlotShort : m_StatusTexts.SlotLong) : 0) +
                           (L.Cpu ? Width(L.Short ? m_StatusTexts.CpuShort : m_StatusTexts.CpuLong) : 0) +
                           (L.Video ? Width(L.Short ? m_StatusTexts.VideoShort : m_StatusTexts.VideoLong) : 0) + (L.Message && !m_StatusTexts.Message.isEmpty() ? MessageMinimum : 0);
        if (Needed <= Available)
        {
            Chosen = &L;
            break;
        }
    }
    auto Show = [](QLabel * Label, bool Visible, const QString & Text, const QString & Full) {
        Label->setText(Visible ? Text : QString());
        Label->setVisible(Visible && !Text.isEmpty());
        Label->setToolTip(Full);
    };
    Show(m_SlotLabel, Chosen->Slot, Chosen->Short ? m_StatusTexts.SlotShort : m_StatusTexts.SlotLong, m_StatusTexts.SlotLong);
    Show(m_CpuLabel, Chosen->Cpu, Chosen->Short ? m_StatusTexts.CpuShort : m_StatusTexts.CpuLong, m_StatusTexts.CpuLong);
    Show(m_VideoLabel, Chosen->Video, Chosen->Short ? m_StatusTexts.VideoShort : m_StatusTexts.VideoLong, m_StatusTexts.VideoLong);
    // the message: the width that the others leave
    int Others = Fixed;
    for (const QLabel * L : {m_SlotLabel, m_CpuLabel, m_VideoLabel})
    {
        Others += L->isVisibleTo(this) ? L->sizeHint().width() + Gap : 0;
    }
    const int Room = std::max(0, Available - Others);
    m_StatusLabel->setVisible(Chosen->Message && !m_StatusTexts.Message.isEmpty());
    m_StatusLabel->setText(Metrics.elidedText(m_StatusTexts.Message, Qt::ElideRight, Room));
    m_StatusLabel->setToolTip(m_StatusTexts.Message);
    // what is hidden stays reachable: the tooltip of the state
    QStringList Hidden;
    for (const QString & Full : {m_StatusTexts.SlotLong, m_StatusTexts.CpuLong, m_StatusTexts.VideoLong})
    {
        if (!Full.isEmpty())
        {
            Hidden << Full;
        }
    }
    m_StateLabel->setToolTip(Hidden.join(QStringLiteral("  ·  ")));
}

void MainWindow::OnRomListChanged()
{
    if (m_RomBrowser == nullptr)
    {
        return;
    }
    m_RomBrowser->RefreshRomList();
    m_RomBrowser->HighLightLastRom();
    if (RomBrowserShown())
    {
        m_RomBrowser->WatchStart(); // the subdirectories that are watched
    }
}

void MainWindow::OnRomBrowserEnabled()
{
    ShowRomBrowser(true);
}

void MainWindow::OnScreenSaverSetting()
{
    UpdateScreenSaver();
}

// WM_SYSCOMMAND SC_SCREENSAVE / SC_MONITORPOWER are refused while the CPU runs and is not paused
// and Setting_DisableScrSaver is on: the desktop is asked not to idle for that time
void MainWindow::UpdateScreenSaver()
{
    m_ScreenSaver->Set(!m_ShuttingDown && g_Settings->LoadBool(GameRunning_CPU_Running) && !g_Settings->LoadBool(GameRunning_CPU_Paused) && UISettingsLoadBool(Setting_DisableScrSaver));
}

// CMainGui::BringToTop
void MainWindow::BringToTop()
{
    raise();
    activateWindow();
}

// ---- place and size ----

// The middle of the screen unless the settings hold a place (UserInterface_MainWindowTop/Left
// for a game, RomBrowser_Top/Left for the browser)
void MainWindow::RestoreWindowPosition(bool Browser)
{
    QScreen * Screen = QGuiApplication::primaryScreen();
    int X = 0, Y = 0;
    if (Screen != nullptr)
    {
        const QRect Area = Screen->availableGeometry();
        X = Area.x() + (Area.width() - width()) / 2;
        Y = Area.y() + (Area.height() - height()) / 2;
    }
    UISettingsLoadDword(Browser ? RomBrowser_Left : UserInterface_MainWindowLeft, (uint32_t &)X);
    UISettingsLoadDword(Browser ? RomBrowser_Top : UserInterface_MainWindowTop, (uint32_t &)Y);
    move(X, Y);
}

// CRomBrowser::FixRomListWindow: the window can be resized and has the size the browser had
void MainWindow::EnterBrowserMode()
{
    m_RenderSize = QSize();
    if (isFullScreen())
    {
        return;
    }
    layout()->setSizeConstraint(QLayout::SetDefaultConstraint);
    // The library window has the size it had (Vortex64's own state; the classic browser's
    // RomBrowser_Width/Height are for a table)
    const QSize Saved(Vortex::Store::Instance().Value("window.w", 1180).toInt(), Vortex::Store::Instance().Value("window.h", 720).toInt());
    const QSize Size = Saved.expandedTo(QSize(900, 580));
    UpdateChrome(); // (before the size: the menu and status bars are part of the window)
    m_Stack->setMinimumSize(900, 580);
    m_Stack->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    m_Stack->setFixedSize(Size);
    adjustSize();
    m_Stack->setMinimumSize(900, 580);
    m_Stack->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    RestoreWindowPosition(true);
}

// CRomBrowser::HideRomList / CMainGui::ChangeWinSize: a fixed video area of 640 x 480 (what the
// video plugin starts with), not maximised
void MainWindow::EnterGameMode()
{
    if (isFullScreen())
    {
        return;
    }
    if (isMaximized())
    {
        UISettingsSaveBool(RomBrowser_Maximized, true);
        showNormal();
    }
    UpdateChrome();
    m_Stack->setMinimumSize(0, 0);
    m_Stack->setFixedSize(640, 480);
    layout()->setSizeConstraint(QLayout::SetFixedSize);
    adjustSize();
    RestoreWindowPosition(false);
}

void MainWindow::SetRenderSize(int Width, int Height)
{
    if (Width < 160 || Height < 120 || Width > 16384 || Height > 16384)
    {
        return;
    }
    if (m_ShuttingDown || RomBrowserShown())
    {
        return;
    }
    m_RenderSize = QSize(Width, Height);
    if (isFullScreen() || isMaximized())
    {
        return;
    }
    m_Stack->setFixedSize(Width, Height);
    layout()->setSizeConstraint(QLayout::SetFixedSize);
    adjustSize();
}

void MainWindow::moveEvent(QMoveEvent * Event)
{
    QMainWindow::moveEvent(Event);
    if (m_ShuttingDown || !isVisible() || isMinimized() || isMaximized() || isFullScreen())
    {
        return;
    }
    if (RomBrowserShown())
    {
        m_SaveBrowserPosition = true;
        m_BrowserPosition = pos();
    }
    else
    {
        m_SaveMainPosition = true;
        m_MainPosition = pos();
    }
    m_SavePositionTimer.start();
}

// ResizeRomList: the size of the browser is remembered unless the window is maximised
void MainWindow::resizeEvent(QResizeEvent * Event)
{
    QMainWindow::resizeEvent(Event);
    FitStatusBar();
    if (m_ShuttingDown || !isVisible() || isFullScreen() || !RomBrowserShown() || isMaximized())
    {
        return;
    }
    const QSize Size = m_Stack->size();
    if (Size.height() != 0)
    {
        Vortex::Store::Instance().SetValue("window.w", Size.width());
        Vortex::Store::Instance().SetValue("window.h", Size.height());
    }
}

// CMainGui::SaveWindowLoc
void MainWindow::SaveWindowLocation()
{
    bool Flush = false;
    if (m_SaveMainPosition)
    {
        m_SaveMainPosition = false;
        UISettingsSaveDword(UserInterface_MainWindowTop, m_MainPosition.y());
        UISettingsSaveDword(UserInterface_MainWindowLeft, m_MainPosition.x());
        Flush = true;
    }
    if (m_SaveBrowserPosition)
    {
        m_SaveBrowserPosition = false;
        UISettingsSaveDword(RomBrowser_Top, m_BrowserPosition.y());
        UISettingsSaveDword(RomBrowser_Left, m_BrowserPosition.x());
        Flush = true;
    }
    if (Flush)
    {
        CSettingTypeApplication::Flush();
    }
}

// ---- focus, drag and drop ----

void MainWindow::changeEvent(QEvent * Event)
{
    QMainWindow::changeEvent(Event);
    if (Event->type() == QEvent::ActivationChange)
    {
        WindowFocusChanged(isActiveWindow());
    }
    else if (Event->type() == QEvent::WindowStateChange && RomBrowserShown() && !isFullScreen() && !isMinimized())
    {
        UISettingsSaveBool(RomBrowser_Maximized, isMaximized());
    }
}

// WM_SETFOCUS / WM_KILLFOCUS of the main window
void MainWindow::WindowFocusChanged(bool Gained)
{
    if (m_ShuttingDown || RomBrowserShown() || g_BaseSystem == nullptr || !g_Settings->LoadBool(GameRunning_CPU_Running))
    {
        return;
    }
    const bool AutoSleep = g_Settings->LoadBool((SettingID)Setting_AutoSleep);
    if (Gained)
    {
        if (AutoSleep)
        {
            g_BaseSystem->ExternalEvent(SysEvent_ResumeCPU_AppGainedFocus);
        }
        return;
    }
    if (AutoSleep)
    {
        g_BaseSystem->ExternalEvent(SysEvent_PauseCPU_AppLostFocus);
    }
    if (g_Plugins != nullptr && g_Plugins->Control() != nullptr && g_Plugins->Control()->WM_KillFocus != nullptr)
    {
        g_Plugins->Control()->WM_KillFocus(0, 0);
    }
}

// WM_ACTIVATEAPP
void MainWindow::ApplicationActiveChanged(bool Active)
{
    if (m_ShuttingDown || g_BaseSystem == nullptr || !g_Settings->LoadBool(GameRunning_CPU_Running))
    {
        return;
    }
    if (RomBrowserShown())
    {
        return;
    }
    const bool AutoSleep = g_Settings->LoadBool((SettingID)Setting_AutoSleep);
    if (!Active && UISettingsLoadBool(UserInterface_InFullScreen) && UISettingsLoadBool(UserInterface_ExitFullscreenOnLoseFocus))
    {
        LeaveFullScreen();
        return;
    }
    if (AutoSleep || Active)
    {
        g_BaseSystem->ExternalEvent(Active ? SysEvent_ResumeCPU_AppGainedActive : SysEvent_PauseCPU_AppLostActive);
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent * Event)
{
    if (Event->mimeData()->hasUrls())
    {
        Event->acceptProposedAction();
    }
}

// WM_DROPFILES: the first file runs, as a disk image when it is one
bool MainWindow::DropFiles(const QList<QUrl> & Urls)
{
    for (const QUrl & Url : Urls)
    {
        if (Url.isLocalFile())
        {
            OpenImage(Url.toLocalFile());
            return true;
        }
    }
    return false;
}

void MainWindow::dropEvent(QDropEvent * Event)
{
    if (DropFiles(Event->mimeData()->urls()))
    {
        Event->acceptProposedAction();
    }
}
