#include "Debugger/DebuggerUI.h"
#include "MainMenu.h"
#include "VirtualKeys.h"
#include "Vortex/Language.h"
#include "Vortex/Strings.h"
#include "MainWindow.h"
#include "Dialogs/CheatDialog.h"
#include "Dialogs/EnhancementDialog.h"
#include "Dialogs/AboutDialog.h"
#include "Dialogs/LogOptionsDialog.h"

#include <Common/path.h>
#include <Project64-core/Multilanguage.h>
#include <Project64-core/N64System/N64Disk.h>
#include <Project64-core/Notification.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/Settings.h>
#include <Project64-core/Settings/DebugSettings.h>
#include <Project64-core/Settings/SettingsID.h>
#include <Project64/Settings/UISettings.h>

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QTimer>
#include <QUrl>

namespace
{
QString FromStd(const std::string & Text)
{
    return QString::fromUtf8(Text.c_str());
}

QString Lang(LanguageStringID ID)
{
    return QString::fromUtf8(GS(ID));
}

bool CpuRunning(void)
{
    return g_Settings->LoadBool(GameRunning_CPU_Running);
}

CMenuShortCutKey::RUNNING_STATE CurrentRunningState(void)
{
    return CMenuShortCutKey::RunningState();
}

const char * const RomFilter = "N64 ROMs (*.z64 *.n64 *.v64 *.rom *.usa *.jap *.pal *.bin *.zip *.7z *.ndd *.d64);;All files (*)";
const char * const RomOnlyFilter = "N64 ROMs (*.z64 *.n64 *.v64 *.rom *.usa *.jap *.pal *.bin *.zip *.7z);;All files (*)";
const char * const DiskFilter = "N64DD disk images (*.ndd *.d64);;All files (*)";
const char * const SaveFilter = "Vortex64 / Project64 saves (*.pj *.pj? *.zip)";
} // namespace

QtMainMenu::QtMainMenu(MainWindow * Window) :
    QObject(Window),
    m_Window(Window),
    m_Holder(nullptr)
{
    QTimer * PollTimer = new QTimer(this);
    connect(PollTimer, &QTimer::timeout, this, &QtMainMenu::Poll);
    PollTimer->start(200);
    ResetMenu();
}

QtMainMenu::~QtMainMenu()
{
    delete m_Holder;
}

// The Windows GUI rebuilds the menu from settings change callbacks; this
// compares the values FillOutMenu() reads instead.
QString QtMainMenu::StateKey(void) const
{
    QStringList Parts;
    auto B = [&](bool Value) { Parts << (Value ? "1" : "0"); };
    B(g_Settings->LoadBool(UserInterface_BasicMode));
    B(CpuRunning());
    B(g_Settings->LoadBool(GameRunning_CPU_Paused));
    B(g_Settings->LoadBool(GameRunning_LoadingInProgress));
    Parts << FromStd(g_Settings->LoadStringVal(Game_GameName));
    B(UISettingsLoadBool(UserInterface_InFullScreen));
    B(UISettingsLoadBool(RomBrowser_Enabled));
    B(g_Settings->LoadBool(Setting_Enhancement));
    B(g_Settings->LoadBool(GameRunning_LimitFPS));
    B(UISettingsLoadBool(UserInterface_AlwaysOnTop));
    B(g_Settings->LoadBool(UserInterface_ShowCPUPer));
    B(g_Disk != nullptr);
    Parts << QString::number(g_Settings->LoadDword(Game_CurrentSaveState));
    Parts << QString::number(UISettingsLoadDword(File_RecentGameFileCount)) << QString::number(UISettingsLoadDword(Directory_RecentGameDirCount));
    Parts << FromStd(UISettingsLoadStringIndex(File_RecentGameFileIndex, 0)) << FromStd(UISettingsLoadStringIndex(File_RecentGameFileIndex, 1))
          << FromStd(UISettingsLoadStringIndex(Directory_RecentGameDirIndex, 0));
    if (g_Plugins != nullptr)
    {
        B(g_Plugins->Gfx() != nullptr && g_Plugins->Gfx()->DllConfig != nullptr);
        B(g_Plugins->Gfx() != nullptr && g_Plugins->Gfx()->ChangeWindow != nullptr);
        B(g_Plugins->Audio() != nullptr && g_Plugins->Audio()->DllConfig != nullptr);
        B(g_Plugins->RSP() != nullptr && g_Plugins->RSP()->DllConfig != nullptr);
        B(g_Plugins->Control() != nullptr && g_Plugins->Control()->DllConfig != nullptr);
    }
    if (g_Lang != nullptr)
    {
        for (LanguageFile & File : g_Lang->GetLangList())
        {
            if (g_Lang->IsCurrentLang(File))
            {
                Parts << FromStd(File.LanguageName);
            }
        }
    }
    if (g_DebugSettings.haveDebugger)
    {
        Parts << "debugger";
        static const SettingID DebugSettingIDs[] = {
            Debugger_EndOnPermLoop, Debugger_BreakOnUnhandledMemory, Debugger_BreakOnAddressError, Debugger_StepOnBreakOpCode,
            Debugger_ShowPifErrors, Debugger_ShowDListAListCount, Debugger_DebugLanguage, Debugger_ShowRecompMemSize,
            Debugger_RecordRecompilerAsm, Debugger_DisableGameFixes, Debugger_AppLogFlush, Logging_GenerateLog,
            Debugger_RecordExecutionTimes};
        for (SettingID ID : DebugSettingIDs)
        {
            B(g_Settings->LoadBool(ID));
        }
        static const SettingID TraceIDs[] = {
            Debugger_TraceMD5, Debugger_TraceSettings, Debugger_TraceUnknown, Debugger_TraceAppInit, Debugger_TraceAppCleanup,
            Debugger_TraceN64System, Debugger_TracePlugins, Debugger_TraceVideoPlugin, Debugger_TraceAudioPlugin,
            Debugger_TraceControllerPlugin, Debugger_TraceRSPPlugin, Debugger_TraceRSP, Debugger_TraceAudio,
            Debugger_TraceRegisterCache, Debugger_TraceRecompiler, Debugger_TraceTLB, Debugger_TraceUserInterface};
        for (SettingID ID : TraceIDs)
        {
            B(g_Settings->LoadDword(ID) == TraceVerbose);
        }
    }
    return Parts.join('|');
}

void QtMainMenu::Poll(void)
{
    if (g_Settings == nullptr || QApplication::activePopupWidget() != nullptr)
    {
        return;
    }
    // Settings > Keyboard shortcuts saved a new table (Info_ShortCutsChanged)
    if (UISettingsLoadBool(Info_ShortCutsChanged))
    {
        UISettingsSaveBool(Info_ShortCutsChanged, false);
        ShortCutsChanged();
        return;
    }
    const QString Key = StateKey();
    if (Key != m_StateKey)
    {
        ResetMenu();
    }
}

void QtMainMenu::ShortCutsChanged(void)
{
    m_ShortCuts.Load();
    ResetMenu();
}

int QtMainMenu::QtKeyFromVirtualKey(int VirtualKey)
{
    return ::QtKeyFromVirtualKey(VirtualKey);
}

QKeySequence QtMainMenu::KeySequenceFor(const CMenuShortCutKey & Key)
{
    const int QtKey = QtKeyFromVirtualKey(Key.Key());
    if (QtKey == 0)
    {
        return QKeySequence();
    }
    int Modifiers = 0;
    if (Key.Ctrl())
    {
        Modifiers |= Qt::CTRL;
    }
    if (Key.Alt())
    {
        Modifiers |= Qt::ALT;
    }
    if (Key.Shift())
    {
        Modifiers |= Qt::SHIFT;
    }
    return QKeySequence(QtKey | Modifiers);
}

QString QtMainMenu::GetSaveSlotString(int Slot)
{
    static const LanguageStringID SlotNames[] = {MENU_SLOT_DEFAULT, MENU_SLOT_1, MENU_SLOT_2, MENU_SLOT_3, MENU_SLOT_4, MENU_SLOT_5,
                                                 MENU_SLOT_6, MENU_SLOT_7, MENU_SLOT_8, MENU_SLOT_9, MENU_SLOT_10};
    QString SlotName = Lang(SlotNames[Slot]);
    if (!CpuRunning())
    {
        return SlotName;
    }

    // Same candidates and order as CMainMenu::GetSaveSlotString.
    auto LastModified = [](const CPath & FileName) -> QString {
        QFileInfo Info(QString::fromUtf8(((const char *)FileName)));
        if (!Info.exists())
        {
            return QString();
        }
        return Info.lastModified().toString(" (yyyy/MM/dd hh:mm:ss)");
    };

    CPath FileName(g_Settings->LoadStringVal(Directory_InstantSave).c_str(), "");
    if (g_Settings->LoadBool(Setting_UniqueSaveDir))
    {
        FileName.AppendDirectory(g_Settings->LoadStringVal(Game_UniqueSaveDir).c_str());
    }
    FileName.NormalizePath(CPath(CPath::MODULE_DIRECTORY));
    auto Candidate = [&](const std::string & GameName) {
        FileName.SetNameExtension(Slot != 0 ? stdstr_f("%s.pj%d", GameName.c_str(), Slot).c_str() : stdstr_f("%s.pj", GameName.c_str()).c_str());
        QString Last;
        if (g_Settings->LoadDword(Setting_AutoZipInstantSave))
        {
            CPath ZipFileName(FileName.GetDirectory(), stdstr_f("%s.zip", FileName.GetNameExtension().c_str()).c_str());
            Last = LastModified(ZipFileName);
        }
        if (Last.isEmpty())
        {
            Last = LastModified(FileName);
        }
        return Last;
    };
    QString LastSaveTime = Candidate(g_Settings->LoadStringVal(Rdb_GoodName));
    if (LastSaveTime.isEmpty())
    {
        LastSaveTime = Candidate(g_Settings->LoadStringVal(Game_GameName));
    }
    return SlotName + LastSaveTime;
}

void QtMainMenu::SetTraceModuleSetttings(SettingID Type)
{
    const uint32_t Value = g_Settings->LoadDword(Type) == TraceVerbose ? g_Settings->LoadDefaultDword(Type) : TraceVerbose;
    g_Settings->SaveDword(Type, Value);
}

void QtMainMenu::ResetMenu(void)
{
    if (g_Settings == nullptr || g_Lang == nullptr)
    {
        return;
    }
    m_StateKey = StateKey();
    FillOutMenu();
}

// CMainMenu::FillOutMenu, item for item. Shortcuts come from the same CShortCuts
// table (ShortCutString() for the running state) and are installed on the window
// so they work while the menu bar is hidden (full screen).
void QtMainMenu::FillOutMenu(void)
{
    const bool InBasicMode = g_Settings->LoadBool(UserInterface_BasicMode);
    const bool CPURunning = CpuRunning();
    const bool RomLoading = g_Settings->LoadBool(GameRunning_LoadingInProgress);
    const bool RomLoaded = g_Settings->LoadStringVal(Game_GameName).length() > 0;
    const bool RomList = UISettingsLoadBool(RomBrowser_Enabled) && !CPURunning;
    const bool Enhancement = g_Settings->LoadBool(Setting_Enhancement);
    const CMenuShortCutKey::RUNNING_STATE RunningState = CurrentRunningState();

    QMenuBar * Bar = m_Window->menuBar();
    Bar->clear();
    QObject * OldHolder = m_Holder;
    m_Holder = new QObject(this);
    if (OldHolder != nullptr)
    {
        // Actions added to the window must go before the holder that owns them.
        for (QAction * Action : m_Window->actions())
        {
            if (Action->parent() == OldHolder)
            {
                m_Window->removeAction(Action);
            }
        }
    }

    struct ItemOptions
    {
        bool Enabled = true;
        bool Checked = false;
        bool Checkable = false;
    };
    auto AddItem = [&](QMenu * Menu, int MenuID, const QString & Text, ItemOptions Options = ItemOptions()) {
        QAction * Action = Menu->addAction(Text);
        Action->setParent(m_Holder);
        Action->setProperty("menuId", MenuID);
        Action->setEnabled(Options.Enabled);
        if (Options.Checkable || Options.Checked)
        {
            Action->setCheckable(true);
            Action->setChecked(Options.Checked);
        }
        // The shortcut is only installed when the item is enabled and the key is
        // active for the current running state, like the accelerator table.
        const MSC_MAP & Map = m_ShortCuts.GetShortCuts();
        MSC_MAP::const_iterator Entry = Map.find(MenuID);
        if (Entry != Map.end() && Entry->second.Avaliable(RunningState))
        {
            QList<QKeySequence> Sequences;
            for (const CMenuShortCutKey & Key : Entry->second.GetAccelItems())
            {
                if (Key.Inactive() || !Key.Active(RunningState))
                {
                    continue;
                }
                const QKeySequence Sequence = KeySequenceFor(Key);
                if (!Sequence.isEmpty())
                {
                    Sequences.push_back(Sequence);
                }
            }
            if (!Sequences.isEmpty())
            {
                Action->setShortcuts(Sequences);
                Action->setShortcutContext(Qt::WindowShortcut);
                m_Window->addAction(Action);
            }
        }
        QObject::connect(Action, &QAction::triggered, this, [this, MenuID]() { ProcessMessage(MenuID); });
        return Action;
    };
    auto AddSubMenu = [&](QMenu * Parent, const QString & Title, bool Enabled = true) {
        QMenu * Sub = Parent->addMenu(Title);
        Sub->setEnabled(Enabled);
        return Sub;
    };
    auto TopMenu = [&](const QString & Title, bool Enabled = true) {
        QMenu * Menu = Bar->addMenu(Title);
        Menu->setParent(Bar);
        Menu->menuAction()->setEnabled(Enabled);
        return Menu;
    };
    ItemOptions Disabled;
    Disabled.Enabled = false;

    // The menus of Vortex64: File, Emulation, View, Settings, Tools, Help. They hold what the classic File / System /
    // Options / Debugger / Help menus held (the same commands, enable rules and shortcuts); the library window has
    // no menu bar, these show while a game runs.
    auto T = [](const char * Spanish, const char * English) { return Vortex::Tr(Spanish, English); };

    // File
    QMenu * FileMenu = TopMenu(T("&Archivo", "&File"), !RomLoading);
    AddItem(FileMenu, ID_FILE_OPEN_ROM, T("Abrir ROM…", "Open ROM…"));
    AddItem(FileMenu, ID_FILE_OPEN_COMBO, T("Abrir ROM con disco 64DD…", "Open ROM with a 64DD disk…"));
    {
        ItemOptions Options;
        Options.Enabled = RomLoaded;
        AddItem(FileMenu, ID_FILE_ROM_INFO, T("Información de la ROM", "ROM information"), Options);
    }
    FileMenu->addSeparator();
    {
        ItemOptions Options;
        Options.Enabled = CPURunning;
        AddItem(FileMenu, ID_FILE_ENDEMULATION, T("Cerrar juego y volver a la biblioteca", "Close game and go back to the library"), Options);
    }

    QStringList RecentRoms;
    const uint32_t RomsToRemember = UISettingsLoadDword(File_RecentGameFileCount);
    for (uint32_t Count = 0; Count < RomsToRemember; Count++)
    {
        const std::string LastRom = UISettingsLoadStringIndex(File_RecentGameFileIndex, Count);
        if (LastRom.empty())
        {
            break;
        }
        RecentRoms << QString("&%1 %2").arg((Count + 1) % 10).arg(FromStd(LastRom));
    }
    QStringList RecentDirs;
    const uint32_t DirsToRemember = UISettingsLoadDword(Directory_RecentGameDirCount);
    for (uint32_t Count = 0; Count < DirsToRemember; Count++)
    {
        const std::string LastDir = UISettingsLoadStringIndex(Directory_RecentGameDirIndex, Count);
        if (LastDir.empty())
        {
            break;
        }
        RecentDirs << QString("&%1 %2").arg((Count + 1) % 10).arg(FromStd(LastDir));
    }
    FileMenu->addSeparator();
    QMenu * RecentRomMenu = AddSubMenu(FileMenu, T("ROMs recientes", "Recent ROMs"), !RecentRoms.isEmpty());
    for (int i = 0; i < RecentRoms.size(); i++)
    {
        AddItem(RecentRomMenu, ID_RECENT_ROM_START + i, RecentRoms[i]);
    }
    if (!InBasicMode)
    {
        QMenu * RecentDirMenu = AddSubMenu(FileMenu, T("Carpetas de ROMs recientes", "Recent ROM folders"), !RecentDirs.isEmpty());
        for (int i = 0; i < RecentDirs.size(); i++)
        {
            AddItem(RecentDirMenu, ID_RECENT_DIR_START + i, RecentDirs[i]);
        }
    }
    FileMenu->addSeparator();
    AddItem(FileMenu, ID_FILE_EXIT, T("Salir", "Exit"));

    // Emulation (System of the classic menu)
    if (CPURunning)
    {
        QMenu * SystemMenu = TopMenu(T("&Emulación", "&Emulation"), !RomLoading);
        AddItem(SystemMenu, ID_SYSTEM_PAUSE, g_Settings->LoadBool(GameRunning_CPU_Paused) ? T("Reanudar", "Resume") : T("Pausar", "Pause"));
        if (InBasicMode)
        {
            AddItem(SystemMenu, ID_SYSTEM_RESET_SOFT, T("Reiniciar", "Reset"));
        }
        else
        {
            QMenu * ResetMenu = AddSubMenu(SystemMenu, T("Reiniciar", "Reset"));
            AddItem(ResetMenu, ID_SYSTEM_RESET_SOFT, T("Reinicio suave", "Soft reset"));
            AddItem(ResetMenu, ID_SYSTEM_RESET_HARD, T("Reinicio completo", "Hard reset"));
        }
        AddItem(SystemMenu, ID_SYSTEM_BITMAP, T("Captura de pantalla", "Screenshot"));
        SystemMenu->addSeparator();
        if (!InBasicMode)
        {
            ItemOptions Options;
            Options.Checked = g_Settings->LoadBool(GameRunning_LimitFPS);
            Options.Checkable = true;
            AddItem(SystemMenu, ID_SYSTEM_LIMITFPS, T("Limitar FPS", "Limit FPS"), Options);
            SystemMenu->addSeparator();
        }
        {
            ItemOptions Options;
            Options.Enabled = g_Disk != nullptr;
            AddItem(SystemMenu, ID_SYSTEM_SWAPDISK, T("Cambiar disco 64DD…", "Swap 64DD disk…"), Options);
        }
        SystemMenu->addSeparator();
        AddItem(SystemMenu, ID_SYSTEM_SAVE, T("Guardar estado", "Save state"));
        if (!InBasicMode)
        {
            AddItem(SystemMenu, ID_SYSTEM_SAVEAS, T("Guardar estado como…", "Save state as…"));
        }
        AddItem(SystemMenu, ID_SYSTEM_RESTORE, T("Cargar estado", "Load state"));
        if (!InBasicMode)
        {
            AddItem(SystemMenu, ID_SYSTEM_LOAD, T("Cargar estado desde…", "Load state from…"));
        }
        SystemMenu->addSeparator();

        QMenu * CurrentSaveMenu = AddSubMenu(SystemMenu, T("Ranura de estado", "State slot"));
        const uint32_t CurrentSaveState = g_Settings->LoadDword(Game_CurrentSaveState);
        // The slot labels carry the last save time, so they are refreshed each time
        // the submenu opens (GetSaveSlotString reads the save files).
        static const int SlotIDs[] = {ID_CURRENT_SAVE_DEFAULT, ID_CURRENT_SAVE_1, ID_CURRENT_SAVE_2, ID_CURRENT_SAVE_3, ID_CURRENT_SAVE_4,
                                      ID_CURRENT_SAVE_5, ID_CURRENT_SAVE_6, ID_CURRENT_SAVE_7, ID_CURRENT_SAVE_8, ID_CURRENT_SAVE_9,
                                      ID_CURRENT_SAVE_10};
        std::vector<QAction *> SlotActions;
        for (int Slot = 0; Slot <= 10; Slot++)
        {
            ItemOptions Options;
            Options.Checked = CurrentSaveState == (uint32_t)Slot;
            Options.Checkable = true;
            SlotActions.push_back(AddItem(CurrentSaveMenu, SlotIDs[Slot], GetSaveSlotString(Slot), Options));
            if (Slot == 0)
            {
                CurrentSaveMenu->addSeparator();
            }
        }
        connect(CurrentSaveMenu, &QMenu::aboutToShow, m_Holder, [this, SlotActions]() {
            for (size_t Slot = 0; Slot < SlotActions.size(); Slot++)
            {
                SlotActions[Slot]->setText(GetSaveSlotString((int)Slot));
            }
        });
        SystemMenu->addSeparator();
        AddItem(SystemMenu, ID_SYSTEM_GSBUTTON, T("Botón GameShark", "GameShark button"));
    }

    // View (the window and the display)
    QMenu * ViewMenu = TopMenu(T("&Ver", "&View"), !RomLoading);
    {
        ItemOptions Options;
        Options.Enabled = CPURunning && !(g_Plugins != nullptr && g_Plugins->Gfx() != nullptr && g_Plugins->Gfx()->ChangeWindow == nullptr);
        AddItem(ViewMenu, ID_OPTIONS_FULLSCREEN, T("Pantalla completa", "Full screen"), Options);
    }
    if (!InBasicMode)
    {
        ItemOptions Options;
        Options.Checked = UISettingsLoadDword(UserInterface_AlwaysOnTop) != 0;
        Options.Checkable = true;
        Options.Enabled = CPURunning;
        AddItem(ViewMenu, ID_OPTIONS_ALWAYSONTOP, T("Siempre visible", "Always on top"), Options);
        Options = ItemOptions();
        Options.Checked = g_Settings->LoadDword(UserInterface_ShowCPUPer) != 0;
        Options.Checkable = true;
        AddItem(ViewMenu, ID_OPTIONS_CPU_USAGE, T("Mostrar uso de CPU", "Show CPU usage"), Options);
    }

    // Settings (Options of the classic menu): the window of Vortex64 and the windows of the plugins
    QMenu * OptionMenu = TopMenu(T("&Configuración", "&Settings"), !RomLoading);
    AddItem(OptionMenu, ID_OPTIONS_SETTINGS, T("Ajustes de Vortex64…", "Vortex64 settings…"));
    OptionMenu->addSeparator();
    auto ConfigItem = [&](int MenuID, const QString & Text, bool Available) {
        ItemOptions Options;
        Options.Enabled = Available;
        AddItem(OptionMenu, MenuID, Text, Options);
    };
    ConfigItem(ID_OPTIONS_CONFIG_GFX, T("Configurar vídeo…", "Configure video…"), g_Plugins != nullptr && g_Plugins->Gfx() != nullptr && g_Plugins->Gfx()->DllConfig != nullptr);
    ConfigItem(ID_OPTIONS_CONFIG_AUDIO, T("Configurar audio…", "Configure audio…"), g_Plugins != nullptr && g_Plugins->Audio() != nullptr && g_Plugins->Audio()->DllConfig != nullptr);
    if (!InBasicMode)
    {
        ConfigItem(ID_OPTIONS_CONFIG_RSP, T("Configurar RSP…", "Configure RSP…"), g_Plugins != nullptr && g_Plugins->RSP() != nullptr && g_Plugins->RSP()->DllConfig != nullptr);
    }
    ConfigItem(ID_OPTIONS_CONFIG_CONT, T("Configurar controles…", "Configure input…"), g_Plugins != nullptr && g_Plugins->Control() != nullptr && g_Plugins->Control()->DllConfig != nullptr);

    // Tools
    QMenu * ToolsMenu = TopMenu(T("&Herramientas", "&Tools"), !RomLoading);
    if (CPURunning)
    {
        AddItem(ToolsMenu, ID_SYSTEM_CHEAT, T("Trucos…", "Cheats…"));
        if (Enhancement)
        {
            AddItem(ToolsMenu, ID_SYSTEM_ENHANCEMENT, T("Mejoras…", "Enhancements…"));
        }
        ToolsMenu->addSeparator();
    }

    // Debugger menu (FillOutMenu, same order as upstream)
    if (!InBasicMode && g_DebugSettings.haveDebugger)
    {
        auto Toggle = [&](QMenu * Menu, int MenuID, const char * Text, SettingID Setting) {
            ItemOptions Options;
            Options.Checked = g_Settings->LoadBool(Setting);
            Options.Checkable = true;
            AddItem(Menu, MenuID, QString::fromUtf8(Text), Options);
        };
        auto Trace = [&](QMenu * Menu, int MenuID, const char * Text, SettingID Setting) {
            ItemOptions Options;
            Options.Checked = g_Settings->LoadDword(Setting) == TraceVerbose;
            Options.Checkable = true;
            AddItem(Menu, MenuID, QString::fromUtf8(Text), Options);
        };
        QMenu * DebugMenu = AddSubMenu(ToolsMenu, T("Depurador", "Debugger"), !RomLoading);
        AddItem(DebugMenu, ID_DEBUGGER_BREAKPOINTS, "Commands...");
        AddItem(DebugMenu, ID_DEBUGGER_MEMORY, "View memory...");
        AddItem(DebugMenu, ID_DEBUGGER_SCRIPTS, "Scripts...");
        DebugMenu->addSeparator();
        QMenu * MemoryMenu = AddSubMenu(DebugMenu, "Memory");
        AddItem(MemoryMenu, ID_DEBUGGER_MEMORY, "View...");
        AddItem(MemoryMenu, ID_DEBUGGER_SEARCHMEMORY, "Search...");
        AddItem(MemoryMenu, ID_DEBUGGER_SYMBOLS, "Symbols...");
        AddItem(MemoryMenu, ID_DEBUGGER_DUMPMEMORY, "Dump...");
        AddItem(MemoryMenu, ID_DEBUGGER_TLBENTRIES, "TLB entries...");
        AddItem(MemoryMenu, ID_DEBUGGER_DMALOG, "DMA log...");
        QMenu * R4300Menu = AddSubMenu(DebugMenu, "&R4300i");
        AddItem(R4300Menu, ID_DEBUGGER_BREAKPOINTS, "&Commands...");
        AddItem(R4300Menu, ID_DEBUGGER_CPULOG, "Command log...");
        AddItem(R4300Menu, ID_DEBUGGER_EXCBREAKPOINTS, "Exceptions...");
        AddItem(R4300Menu, ID_DEBUGGER_STACKVIEW, "Stack...");
        AddItem(R4300Menu, ID_DEBUGGER_STACKTRACE, "Stack Trace...");
        Toggle(R4300Menu, ID_DEBUG_DISABLE_GAMEFIX, "Disable game fixes", Debugger_DisableGameFixes);
        QMenu * InterruptMenu = AddSubMenu(R4300Menu, "&Generate interrupt");
        {
            ItemOptions Options;
            Options.Enabled = CPURunning;
            AddItem(InterruptMenu, ID_DEBUGGER_INTERRUPT_SP, "SP interrupt", Options);
            AddItem(InterruptMenu, ID_DEBUGGER_INTERRUPT_SI, "SI interrupt", Options);
            AddItem(InterruptMenu, ID_DEBUGGER_INTERRUPT_AI, "AI interrupt", Options);
            AddItem(InterruptMenu, ID_DEBUGGER_INTERRUPT_VI, "VI interrupt", Options);
            AddItem(InterruptMenu, ID_DEBUGGER_INTERRUPT_PI, "PI interrupt", Options);
            AddItem(InterruptMenu, ID_DEBUGGER_INTERRUPT_DP, "DP interrupt", Options);
        }
        Toggle(R4300Menu, ID_DEBUG_END_ON_PERM_LOOP, "End on perm loop", Debugger_EndOnPermLoop);
        Toggle(R4300Menu, ID_DEBUG_BREAK_ON_UNHANDLED_MEM, "Break on unhandled memory actions", Debugger_BreakOnUnhandledMemory);
        Toggle(R4300Menu, ID_DEBUG_BREAK_ON_ADDRESS_ERROR, "Break on address error", Debugger_BreakOnAddressError);
        Toggle(R4300Menu, ID_DEBUG_STEP_ON_BREAK_OPCODE, "Step on break OpCode", Debugger_StepOnBreakOpCode);
        DebugMenu->addSeparator();
        QMenu * ProfileMenu = AddSubMenu(DebugMenu, "Profile");
        Toggle(ProfileMenu, ID_PROFILE_PROFILE, "Record Execution Times", Debugger_RecordExecutionTimes);
        {
            ItemOptions Options;
            Options.Enabled = CPURunning;
            AddItem(ProfileMenu, ID_PROFILE_RESETCOUNTER, "Reset Counters", Options);
            AddItem(ProfileMenu, ID_PROFILE_GENERATELOG, "Generate Log File", Options);
        }
        QMenu * AppLogMenu = AddSubMenu(DebugMenu, "App logging");
        Trace(AppLogMenu, ID_DEBUGGER_TRACE_MD5, "MD5", Debugger_TraceMD5);
        Trace(AppLogMenu, ID_DEBUGGER_TRACE_SETTINGS, "Settings", Debugger_TraceSettings);
        Trace(AppLogMenu, ID_DEBUGGER_TRACE_UNKNOWN, "Unknown", Debugger_TraceUnknown);
        Trace(AppLogMenu, ID_DEBUGGER_TRACE_APPINIT, "App initialization", Debugger_TraceAppInit);
        Trace(AppLogMenu, ID_DEBUGGER_TRACE_APPCLEANUP, "App cleanup", Debugger_TraceAppCleanup);
        Trace(AppLogMenu, ID_DEBUGGER_TRACE_N64SYSTEM, "N64 system", Debugger_TraceN64System);
        Trace(AppLogMenu, ID_DEBUGGER_TRACE_PLUGINS, "Plugins", Debugger_TracePlugins);
        Trace(AppLogMenu, ID_DEBUGGER_TRACE_GFXPLUGIN, "GFX plugin", Debugger_TraceVideoPlugin);
        Trace(AppLogMenu, ID_DEBUGGER_TRACE_AUDIOPLUGIN, "Audio plugin", Debugger_TraceAudioPlugin);
        Trace(AppLogMenu, ID_DEBUGGER_TRACE_CONTROLLERPLUGIN, "Controller plugin", Debugger_TraceControllerPlugin);
        Trace(AppLogMenu, ID_DEBUGGER_TRACE_RSPPLUGIN, "RSP plugin", Debugger_TraceRSPPlugin);
        Trace(AppLogMenu, ID_DEBUGGER_TRACE_RSP, "RSP", Debugger_TraceRSP);
        Trace(AppLogMenu, ID_DEBUGGER_TRACE_AUDIO, "Audio", Debugger_TraceAudio);
        Trace(AppLogMenu, ID_DEBUGGER_TRACE_REGISTERCACHE, "Register cache", Debugger_TraceRegisterCache);
        Trace(AppLogMenu, ID_DEBUGGER_TRACE_RECOMPILER, "Recompiler", Debugger_TraceRecompiler);
        Trace(AppLogMenu, ID_DEBUGGER_TRACE_TLB, "TLB", Debugger_TraceTLB);
        Trace(AppLogMenu, ID_DEBUGGER_TRACE_USERINTERFACE, "User interface", Debugger_TraceUserInterface);
        AppLogMenu->addSeparator();
        Toggle(AppLogMenu, ID_DEBUGGER_APPLOG_FLUSH, "Auto flush file", Debugger_AppLogFlush);
        QMenu * LoggingMenu = AddSubMenu(DebugMenu, "Logging");
        AddItem(LoggingMenu, ID_DEBUGGER_LOGOPTIONS, "Log options...");
        Toggle(LoggingMenu, ID_DEBUGGER_GENERATELOG, "Generate log", Logging_GenerateLog);
        QMenu * NotificationMenu = AddSubMenu(DebugMenu, "Notification");
        Toggle(NotificationMenu, ID_DEBUG_SHOW_PIF_ERRORS, "On PIF errors", Debugger_ShowPifErrors);
        DebugMenu->addSeparator();
        Toggle(DebugMenu, ID_DEBUG_SHOW_DLIST_COUNT, "Display Alist/Dlist count", Debugger_ShowDListAListCount);
        Toggle(DebugMenu, ID_DEBUG_LANGUAGE, "Debug language", Debugger_DebugLanguage);
        Toggle(DebugMenu, ID_DEBUG_SHOW_RECOMP_MEM_SIZE, "Display recompiler code buffer size", Debugger_ShowRecompMemSize);
        DebugMenu->addSeparator();
        Toggle(DebugMenu, ID_DEBUG_RECORD_RECOMPILER_ASM, "Record recompiler ASM", Debugger_RecordRecompilerAsm);
    }

    // Help
    QMenu * HelpMenu = TopMenu(T("A&yuda", "&Help"), !RomLoading);
    AddItem(HelpMenu, ID_HELP_WEBSITE, T("Sitio web de Project64 (el núcleo de Vortex64)", "Project64 website (the core of Vortex64)"));
    AddItem(HelpMenu, ID_HELP_DISCORD, T("Comunidad en Discord", "Community on Discord"));
    HelpMenu->addSeparator();
    AddItem(HelpMenu, ID_HELP_ABOUT, T("Acerca de Vortex64", "About Vortex64"));

    delete OldHolder;
}

static std::string ChooseInitialDirectory(void)
{
    return g_Settings->LoadStringVal(RomList_GameDir);
}

void QtMainMenu::OnOpenRom(void)
{
    const QString File = QFileDialog::getOpenFileName(m_Window, QString(), FromStd(ChooseInitialDirectory()), RomFilter);
    if (File.isEmpty())
    {
        return;
    }
    m_Window->OpenImage(File);
}

void QtMainMenu::OnOpenCombo(void)
{
    const QString Rom = QFileDialog::getOpenFileName(m_Window, QString(), FromStd(ChooseInitialDirectory()), RomOnlyFilter);
    if (Rom.isEmpty())
    {
        return;
    }
    const QString Disk = QFileDialog::getOpenFileName(m_Window, QString(), FromStd(ChooseInitialDirectory()), DiskFilter);
    if (Disk.isEmpty())
    {
        return;
    }
    m_Window->OpenComboImage(Rom, Disk);
}

void QtMainMenu::OnEndEmulation(void)
{
    if (g_BaseSystem != nullptr)
    {
        DebuggerReleaseStep();
        g_BaseSystem->CloseCpu();
    }
}

void QtMainMenu::OnScreenShot(void)
{
    if (g_Plugins == nullptr || g_Plugins->Gfx() == nullptr || g_Plugins->Gfx()->CaptureScreen == nullptr)
    {
        return;
    }
    const std::string Dir = g_Settings->LoadStringVal(Directory_SnapShot);
    g_Plugins->Gfx()->CaptureScreen(Dir.c_str());
}

void QtMainMenu::OnSaveAs(void)
{
    g_BaseSystem->ExternalEvent(SysEvent_PauseCPU_SaveGame);
    const QString Start = FromStd(UISettingsLoadStringVal(Directory_LastSave));
    QString File = QFileDialog::getSaveFileName(m_Window, QString(), Start, SaveFilter);
    if (!File.isEmpty())
    {
        CPath SaveFile(File.toUtf8().constData());
        std::string Ext = SaveFile.GetExtension();
        if (_stricmp(Ext.c_str(), "pj") == 0 || _stricmp(Ext.c_str(), "zip") == 0)
        {
            SaveFile.SetExtension("");
            Ext = SaveFile.GetExtension();
            if (_stricmp(Ext.c_str(), "pj") == 0)
            {
                SaveFile.SetExtension("");
            }
        }
        g_Settings->SaveString(GameRunning_InstantSaveFile, (const char *)SaveFile);
        UISettingsSaveString(Directory_LastSave, SaveFile.GetDirectory());
        g_BaseSystem->ExternalEvent(SysEvent_SaveMachineState);
    }
    g_BaseSystem->ExternalEvent(SysEvent_ResumeCPU_SaveGame);
}

void QtMainMenu::OnLoadState(void)
{
    g_BaseSystem->ExternalEvent(SysEvent_PauseCPU_LoadGame);
    const QString Start = FromStd(UISettingsLoadStringVal(Directory_LastSave));
    const QString File = QFileDialog::getOpenFileName(m_Window, QString(), Start, SaveFilter);
    if (!File.isEmpty())
    {
        CPath SaveFile(File.toUtf8().constData());
        g_Settings->SaveString(GameRunning_InstantSaveFile, (const char *)SaveFile);
        if (!SaveFile.DirectoryExists())
        {
            SaveFile.DirectoryCreate();
        }
        UISettingsSaveString(Directory_LastSave, SaveFile.GetDirectory());
        g_BaseSystem->ExternalEvent(SysEvent_LoadMachineState);
    }
    g_BaseSystem->ExternalEvent(SysEvent_ResumeCPU_LoadGame);
}

void QtMainMenu::OnSwapDisk(void)
{
    const QString File = QFileDialog::getOpenFileName(m_Window, QString(), FromStd(ChooseInitialDirectory()), DiskFilter);
    if (!File.isEmpty() && g_Disk != nullptr)
    {
        g_Disk->SaveDiskImage();
        g_Disk->SwapDiskImage(File.toUtf8().constData());
    }
}

// CMainMenu::ProcessMessage
bool QtMainMenu::ProcessMessage(int MenuID)
{
    switch (MenuID)
    {
    case ID_FILE_OPEN_ROM: OnOpenRom(); break;
    case ID_FILE_OPEN_COMBO: OnOpenCombo(); break;
    case ID_FILE_ROM_INFO: m_Window->ShowRomInfo(); break;
    case ID_FILE_STARTEMULATION:
        if (g_BaseSystem != nullptr)
        {
            g_BaseSystem->StartEmulation(true);
        }
        break;
    case ID_FILE_ENDEMULATION: OnEndEmulation(); break;
    case ID_FILE_ROMDIRECTORY: m_Window->ChooseRomDirectory(); break;
    case ID_FILE_REFRESHROMLIST: m_Window->RefreshRomList(); break;
    case ID_FILE_EXIT: m_Window->close(); break;
    case ID_SYSTEM_RESET_SOFT: g_BaseSystem->ExternalEvent(SysEvent_ResetCPU_Soft); break;
    case ID_SYSTEM_RESET_HARD: g_BaseSystem->ExternalEvent(SysEvent_ResetCPU_Hard); break;
    case ID_SYSTEM_PAUSE:
        g_BaseSystem->ExternalEvent(g_Settings->LoadBool(GameRunning_CPU_Paused) ? SysEvent_ResumeCPU_FromMenu : SysEvent_PauseCPU_FromMenu);
        break;
    case ID_SYSTEM_BITMAP: OnScreenShot(); break;
    case ID_SYSTEM_LIMITFPS: g_Settings->SaveBool(GameRunning_LimitFPS, !g_Settings->LoadBool(GameRunning_LimitFPS)); break;
    case ID_SYSTEM_SWAPDISK: OnSwapDisk(); break;
    case ID_SYSTEM_SAVE: g_BaseSystem->ExternalEvent(SysEvent_SaveMachineState); break;
    case ID_SYSTEM_SAVEAS: OnSaveAs(); break;
    case ID_SYSTEM_RESTORE: g_BaseSystem->ExternalEvent(SysEvent_LoadMachineState); break;
    case ID_SYSTEM_LOAD: OnLoadState(); break;
    case ID_SYSTEM_ENHANCEMENT: ShowEnhancementDialog(m_Window); break;
    case ID_SYSTEM_CHEAT: ShowCheatDialog(m_Window); break;
    case ID_SYSTEM_GSBUTTON: g_BaseSystem->ExternalEvent(SysEvent_GSButtonPressed); break;
    case ID_OPTIONS_DISPLAY_FR: g_Settings->SaveBool(UserInterface_DisplayFrameRate, !g_Settings->LoadBool(UserInterface_DisplayFrameRate)); break;
    case ID_OPTIONS_CHANGE_FR:
        switch (g_Settings->LoadDword(UserInterface_FrameDisplayType))
        {
        case FR_VIs: g_Settings->SaveDword(UserInterface_FrameDisplayType, FR_DLs); break;
        case FR_DLs: g_Settings->SaveDword(UserInterface_FrameDisplayType, FR_PERCENT); break;
        default: g_Settings->SaveDword(UserInterface_FrameDisplayType, FR_VIs);
        }
        break;
    case ID_OPTIONS_INCREASE_SPEED: g_BaseSystem->AlterSpeed(CSpeedLimiter::INCREASE_SPEED); break;
    case ID_OPTIONS_DECREASE_SPEED: g_BaseSystem->AlterSpeed(CSpeedLimiter::DECREASE_SPEED); break;
    case ID_OPTIONS_FULLSCREEN: g_BaseSystem->ExternalEvent(SysEvent_ChangingFullScreen); break;
    case ID_OPTIONS_ALWAYSONTOP:
        if (UISettingsLoadBool(UserInterface_AlwaysOnTop))
        {
            UISettingsSaveBool(UserInterface_AlwaysOnTop, false);
            m_Window->MakeWindowOnTop(false);
        }
        else
        {
            UISettingsSaveBool(UserInterface_AlwaysOnTop, true);
            m_Window->MakeWindowOnTop(CpuRunning());
        }
        break;
    case ID_OPTIONS_CONFIG_RSP: m_Window->ConfigPlugin(PLUGIN_TYPE_RSP); break;
    case ID_OPTIONS_CONFIG_GFX: m_Window->ConfigPlugin(PLUGIN_TYPE_VIDEO); break;
    case ID_OPTIONS_CONFIG_AUDIO: m_Window->ConfigPlugin(PLUGIN_TYPE_AUDIO); break;
    case ID_OPTIONS_CONFIG_CONT: m_Window->ConfigPlugin(PLUGIN_TYPE_CONTROLLER); break;
    case ID_OPTIONS_CPU_USAGE:
        if (g_Settings->LoadBool(UserInterface_ShowCPUPer))
        {
            g_Settings->SaveBool(UserInterface_ShowCPUPer, false);
            g_Notify->DisplayMessage(0, EMPTY_STRING);
        }
        else
        {
            g_Settings->SaveBool(UserInterface_ShowCPUPer, true);
        }
        break;
    case ID_OPTIONS_SETTINGS: m_Window->ShowSettings(); break;
    case ID_PROFILE_PROFILE:
        g_Settings->SaveBool(Debugger_RecordExecutionTimes, !g_Settings->LoadBool(Debugger_RecordExecutionTimes));
        g_BaseSystem->ExternalEvent(SysEvent_ResetFunctionTimes);
        break;
    case ID_PROFILE_RESETCOUNTER: g_BaseSystem->ExternalEvent(SysEvent_ResetFunctionTimes); break;
    case ID_PROFILE_GENERATELOG: g_BaseSystem->ExternalEvent(SysEvent_DumpFunctionTimes); break;
    case ID_DEBUG_END_ON_PERM_LOOP: g_Settings->SaveBool(Debugger_EndOnPermLoop, !g_Settings->LoadBool(Debugger_EndOnPermLoop)); break;
    case ID_DEBUG_BREAK_ON_UNHANDLED_MEM: g_Settings->SaveBool(Debugger_BreakOnUnhandledMemory, !g_Settings->LoadBool(Debugger_BreakOnUnhandledMemory)); break;
    case ID_DEBUG_BREAK_ON_ADDRESS_ERROR: g_Settings->SaveBool(Debugger_BreakOnAddressError, !g_Settings->LoadBool(Debugger_BreakOnAddressError)); break;
    case ID_DEBUG_STEP_ON_BREAK_OPCODE: g_Settings->SaveBool(Debugger_StepOnBreakOpCode, !g_Settings->LoadBool(Debugger_StepOnBreakOpCode)); break;
    case ID_DEBUG_SHOW_PIF_ERRORS: g_Settings->SaveBool(Debugger_ShowPifErrors, !g_Settings->LoadBool(Debugger_ShowPifErrors)); break;
    case ID_DEBUG_SHOW_DLIST_COUNT:
        g_Settings->SaveBool(Debugger_ShowDListAListCount, !g_Settings->LoadBool(Debugger_ShowDListAListCount));
        g_Notify->DisplayMessage(0, EMPTY_STRING);
        break;
    case ID_DEBUG_LANGUAGE:
        g_Settings->SaveBool(Debugger_DebugLanguage, !g_Settings->LoadBool(Debugger_DebugLanguage));
        g_Lang->LoadCurrentStrings();
        m_Window->ReloadRomBrowserColumns();
        break;
    case ID_DEBUG_SHOW_RECOMP_MEM_SIZE:
        g_Settings->SaveBool(Debugger_ShowRecompMemSize, !g_Settings->LoadBool(Debugger_ShowRecompMemSize));
        g_Notify->DisplayMessage(0, EMPTY_STRING);
        break;
    case ID_DEBUG_RECORD_RECOMPILER_ASM: g_Settings->SaveBool(Debugger_RecordRecompilerAsm, !g_Settings->LoadBool(Debugger_RecordRecompilerAsm)); break;
    case ID_DEBUG_DISABLE_GAMEFIX: g_Settings->SaveBool(Debugger_DisableGameFixes, !g_Settings->LoadBool(Debugger_DisableGameFixes)); break;
    case ID_DEBUGGER_DUMPMEMORY: g_Debugger->OpenMemoryDump(); break;
    case ID_DEBUGGER_SEARCHMEMORY: g_Debugger->OpenMemorySearch(); break;
    case ID_DEBUGGER_MEMORY: g_Debugger->OpenMemoryWindow(); break;
    case ID_DEBUGGER_TLBENTRIES: g_Debugger->OpenTLBWindow(); break;
    case ID_DEBUGGER_BREAKPOINTS: g_Debugger->OpenCommandWindow(); break;
    case ID_DEBUGGER_SCRIPTS: g_Debugger->OpenScriptsWindow(); break;
    case ID_DEBUGGER_SYMBOLS: g_Debugger->OpenSymbolsWindow(); break;
    case ID_DEBUGGER_DMALOG: g_Debugger->OpenDMALogWindow(); break;
    case ID_DEBUGGER_CPULOG: g_Debugger->OpenCPULogWindow(); break;
    case ID_DEBUGGER_EXCBREAKPOINTS: g_Debugger->OpenExcBreakpointsWindow(); break;
    case ID_DEBUGGER_STACKTRACE: g_Debugger->OpenStackTraceWindow(); break;
    case ID_DEBUGGER_STACKVIEW: g_Debugger->OpenStackViewWindow(); break;
    case ID_DEBUGGER_TRACE_MD5: SetTraceModuleSetttings(Debugger_TraceMD5); break;
    case ID_DEBUGGER_TRACE_SETTINGS: SetTraceModuleSetttings(Debugger_TraceSettings); break;
    case ID_DEBUGGER_TRACE_UNKNOWN: SetTraceModuleSetttings(Debugger_TraceUnknown); break;
    case ID_DEBUGGER_TRACE_APPINIT: SetTraceModuleSetttings(Debugger_TraceAppInit); break;
    case ID_DEBUGGER_TRACE_APPCLEANUP: SetTraceModuleSetttings(Debugger_TraceAppCleanup); break;
    case ID_DEBUGGER_TRACE_N64SYSTEM: SetTraceModuleSetttings(Debugger_TraceN64System); break;
    case ID_DEBUGGER_TRACE_PLUGINS: SetTraceModuleSetttings(Debugger_TracePlugins); break;
    case ID_DEBUGGER_TRACE_GFXPLUGIN: SetTraceModuleSetttings(Debugger_TraceVideoPlugin); break;
    case ID_DEBUGGER_TRACE_AUDIOPLUGIN: SetTraceModuleSetttings(Debugger_TraceAudioPlugin); break;
    case ID_DEBUGGER_TRACE_CONTROLLERPLUGIN: SetTraceModuleSetttings(Debugger_TraceControllerPlugin); break;
    case ID_DEBUGGER_TRACE_RSPPLUGIN: SetTraceModuleSetttings(Debugger_TraceRSPPlugin); break;
    case ID_DEBUGGER_TRACE_RSP: SetTraceModuleSetttings(Debugger_TraceRSP); break;
    case ID_DEBUGGER_TRACE_AUDIO: SetTraceModuleSetttings(Debugger_TraceAudio); break;
    case ID_DEBUGGER_TRACE_REGISTERCACHE: SetTraceModuleSetttings(Debugger_TraceRegisterCache); break;
    case ID_DEBUGGER_TRACE_RECOMPILER: SetTraceModuleSetttings(Debugger_TraceRecompiler); break;
    case ID_DEBUGGER_TRACE_TLB: SetTraceModuleSetttings(Debugger_TraceTLB); break;
    case ID_DEBUGGER_TRACE_USERINTERFACE: SetTraceModuleSetttings(Debugger_TraceUserInterface); break;
    case ID_DEBUGGER_APPLOG_FLUSH: g_Settings->SaveBool(Debugger_AppLogFlush, !g_Settings->LoadBool(Debugger_AppLogFlush)); break;
    case ID_DEBUGGER_LOGOPTIONS: ShowLogOptionsDialog(m_Window); break;
    case ID_DEBUGGER_GENERATELOG: g_Settings->SaveBool(Logging_GenerateLog, !g_Settings->LoadBool(Logging_GenerateLog)); break;
    case ID_DEBUGGER_INTERRUPT_SP: g_BaseSystem->ExternalEvent(SysEvent_Interrupt_SP); break;
    case ID_DEBUGGER_INTERRUPT_SI: g_BaseSystem->ExternalEvent(SysEvent_Interrupt_SI); break;
    case ID_DEBUGGER_INTERRUPT_AI: g_BaseSystem->ExternalEvent(SysEvent_Interrupt_AI); break;
    case ID_DEBUGGER_INTERRUPT_VI: g_BaseSystem->ExternalEvent(SysEvent_Interrupt_VI); break;
    case ID_DEBUGGER_INTERRUPT_PI: g_BaseSystem->ExternalEvent(SysEvent_Interrupt_PI); break;
    case ID_DEBUGGER_INTERRUPT_DP: g_BaseSystem->ExternalEvent(SysEvent_Interrupt_DP); break;
    case ID_CURRENT_SAVE_DEFAULT:
    case ID_CURRENT_SAVE_1:
    case ID_CURRENT_SAVE_2:
    case ID_CURRENT_SAVE_3:
    case ID_CURRENT_SAVE_4:
    case ID_CURRENT_SAVE_5:
    case ID_CURRENT_SAVE_6:
    case ID_CURRENT_SAVE_7:
    case ID_CURRENT_SAVE_8:
    case ID_CURRENT_SAVE_9:
    case ID_CURRENT_SAVE_10:
    {
        // ID_CURRENT_SAVE_DEFAULT follows ID_CURRENT_SAVE_10 in the enum
        const int Slot = MenuID == ID_CURRENT_SAVE_DEFAULT ? 0 : (MenuID - ID_CURRENT_SAVE_1) + 1;
        g_Notify->DisplayMessage(3, stdstr_f(GS(MENU_SLOT_SAVE), GetSaveSlotString(Slot).toUtf8().constData()).c_str());
        g_Settings->SaveDword(Game_CurrentSaveState, (uint32_t)Slot);
        break;
    }
    case ID_HELP_SUPPORT_PROJECT64: QDesktopServices::openUrl(QUrl("https://www.pj64-emu.com/support-project64.html")); break;
    case ID_HELP_DISCORD: QDesktopServices::openUrl(QUrl("https://discord.gg/Cg3zquF")); break;
    case ID_HELP_WEBSITE: QDesktopServices::openUrl(QUrl("http://www.pj64-emu.com")); break;
    case ID_HELP_ABOUT: ShowAboutDialog(m_Window); break;
    default:
        if (MenuID >= ID_RECENT_ROM_START && MenuID < ID_RECENT_ROM_END)
        {
            const std::string FileName = UISettingsLoadStringIndex(File_RecentGameFileIndex, MenuID - ID_RECENT_ROM_START);
            if (!FileName.empty())
            {
                m_Window->OpenImage(FromStd(FileName));
            }
            break;
        }
        if (MenuID >= ID_RECENT_DIR_START && MenuID < ID_RECENT_DIR_END)
        {
            const std::string Dir = UISettingsLoadStringIndex(Directory_RecentGameDirIndex, MenuID - ID_RECENT_DIR_START);
            if (!Dir.empty())
            {
                g_Settings->SaveString(RomList_GameDir, Dir.c_str());
                m_Window->AddRecentDirectory(FromStd(Dir));
                m_Window->RefreshRomList();
            }
            break;
        }
        if (MenuID >= ID_LANG_START && MenuID < ID_LANG_END)
        {
            // only the languages that are translated (Vortex/Language.h): Spanish and English
            const QStringList Codes = Vortex::SupportedLanguages();
            const int Index = MenuID - ID_LANG_START;
            if (Index < Codes.size())
            {
                Vortex::ApplyLanguage(Codes[Index]);
                m_Window->ReloadRomBrowserColumns();
            }
            break;
        }
        return false;
    }
    return true;
}
