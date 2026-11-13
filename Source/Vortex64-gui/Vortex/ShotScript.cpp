#include "../stdafx.h"
#include "ShotScript.h"
#include "../MainWindow.h"
#include "../Settings/SettingsDialog.h"
#include "../Dialogs/AboutDialog.h"
#include "../MainMenu.h"
#include "Language.h"
#include "Shell.h"
#include "../Debugger/DebuggerUI.h"
#include "Store.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QDir>
#include <QMessageBox>
#include <QGuiApplication>
#include <QScreen>
#include <QLineEdit>
#include <QPixmap>
#include <QToolButton>
#include <QTabWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QDialog>
#include <Vortex64-core/N64System/SystemGlobals.h>
#include <Vortex64-core/Plugins/Plugin.h>
#include <QTimer>
#include <cstdio>

namespace Vortex
{
namespace
{
struct Runner : QObject
{
    MainWindow * Window;
    QString Dir;
    QStringList Steps;
    int Next = 0;
    QWidget * Dialog = nullptr;

    void Step()
    {
        if (Next >= Steps.size())
        {
            Window->close();
            QApplication::quit();
            return;
        }
        const QString S = Steps[Next++];
        const QString Name = S.section(':', 0, 0);
        const QString Arg = S.section(':', 1);
        int Delay = 250;
        Shell * Sh = Window->LibraryShell();
        if (Name == "seed" && Sh != nullptr)
        {
            Store & St = Store::Instance();
            LibraryModel * M = Sh->Model();
            int Played = 0;
            for (int i = 0; i < M->Count() && Played < 9; i++)
            {
                const Entry & E = M->At(i);
                if (E.IsDisk)
                {
                    continue;
                }
                if (Played < 6)
                {
                    St.SetFavorite(E.Path, true);
                }
                St.SessionStart(E.Path);
                St.SessionEnd();
                Played++;
            }
        }
        else if (Name == "page" && Sh != nullptr)
        {
            Sh->ShowPage(Arg);
        }
        else if (Name == "mode" && Sh != nullptr)
        {
            if (QToolButton * B = Sh->ViewButton(Arg))
            {
                B->click();
            }
        }
        else if (Name == "select" && Sh != nullptr)
        {
            const QString Id = Sh->CurrentPage();
            GameBrowser * B = Id == "disks" ? Sh->Disks()->Browser() : Sh->Page(Id)->Browser();
            const QModelIndex I = B->Proxy()->index(Arg.toInt(), 0);
            B->SelectPath(I.data(LibraryModel::PathRole).toString());
        }
        else if (Name == "search" && Sh != nullptr)
        {
            Sh->SearchBox()->setText(Arg);
        }
        else if (Name == "size")
        {
            Window->resize(Arg.section('x', 0, 0).toInt(), Arg.section('x', 1).toInt());
        }
        else if (Name == "wait")
        {
            Delay = Arg.toInt();
        }
        else if (Name == "shot")
        {
            QDir().mkpath(Dir);
            const QString File = Dir + "/" + Arg + ".png";
            const bool Ok = Window->grab().save(File);
            printf("shot %s %s\n", File.toUtf8().constData(), Ok ? "ok" : "FAILED");
            fflush(stdout);
        }
        else if (Name == "screen")
        {
            // the whole screen (to see the windows that are not known by name)
            QDir().mkpath(Dir);
            const QString File = Dir + "/" + Arg + ".png";
            printf("shot %s %s\n", File.toUtf8().constData(), QGuiApplication::primaryScreen()->grabWindow(0).save(File) ? "ok" : "FAILED");
            fflush(stdout);
        }
        else if (Name == "dialog")
        {
            if (Dialog != nullptr)
            {
                delete Dialog;
                Dialog = nullptr;
            }
            const QString Kind = Arg.section(':', 0, 0);
            if (Kind == "settings")
            {
                SettingsDialog * D = new SettingsDialog(false, Window);
                D->ShowPage(Arg.section(':', 1));
                Dialog = D;
            }
            if (Dialog != nullptr)
            {
                Dialog->setAttribute(Qt::WA_DeleteOnClose, false);
                Dialog->show();
            }
        }
        else if (Name == "shotdlg" && Dialog != nullptr)
        {
            QDir().mkpath(Dir);
            const QString File = Dir + "/" + Arg + ".png";
            const bool Ok = Dialog->grab().save(File);
            printf("shot %s %s\n", File.toUtf8().constData(), Ok ? "ok" : "FAILED");
            fflush(stdout);
        }
        else if (Name == "open")
        {
            // open:about | debugger | settings:<category> | cheats | rominfo
            const QString Kind = Arg.section(':', 0, 0);
            if (Kind == "about")
            {
                QTimer::singleShot(0, this, [this]() { ShowAboutDialog(Window); });
            }
            else if (Kind == "debugger")
            {
                Window->OpenDebugger();
            }
            else if (Kind == "cheats")
            {
                QTimer::singleShot(0, this, [this]() { Window->Menu()->ProcessMessage(ID_SYSTEM_CHEAT); });
            }
            else if (Kind == "rominfo")
            {
                QTimer::singleShot(0, this, [this]() { Window->Menu()->ProcessMessage(ID_FILE_ROM_INFO); });
            }
            Delay = 800;
        }
        else if (Name == "playenv")
        {
            // the ROM of VORTEX_TEST_ROM (a real game of the tests)
            Window->OpenRomPath(qEnvironmentVariable("VORTEX_TEST_ROM"));
            Delay = 500;
        }
        else if (Name == "report")
        {
            // report:<tag>: what the window is (a game runs, the library shows): for the tools that look at the screen
            printf("state %s running=%d library=%d window=%dx%d\n", Arg.toUtf8().constData(), (int)g_Settings->LoadBool(GameRunning_CPU_Running), (int)Window->RomBrowserShown(), Window->width(), Window->height());
            fflush(stdout);
        }
        else if (Name == "play")
        {
            // play:<path of a ROM>: opens that game (the one that runs is ended first by the window)
            Window->OpenRomPath(Arg);
            Delay = 500;
        }
        else if (Name == "endgame")
        {
            Window->Menu()->ProcessMessage(ID_FILE_ENDEMULATION);
            Delay = 1500;
        }
        else if (Name == "menu")
        {
            // menu:<n>: opens the n-th menu of the menu bar and grabs the window with it (shot name in the 2nd part)
            QMenuBar * Bar = Window->menuBar();
            const QList<QAction *> Actions = Bar->actions();
            const int Index = Arg.section(':', 0, 0).toInt();
            if (Index < Actions.size() && Actions[Index]->menu() != nullptr)
            {
                QMenu * M = Actions[Index]->menu();
                M->popup(Bar->mapToGlobal(Bar->actionGeometry(Actions[Index]).bottomLeft()));
                QDir().mkpath(Dir);
                QTimer::singleShot(200, this, [M, this, File = Dir + "/" + Arg.section(':', 1) + ".png"]() {
                    printf("shot %s %s\n", File.toUtf8().constData(), M->grab().save(File) ? "ok" : "FAILED");
                    fflush(stdout);
                    M->close();
                });
            }
            Delay = 600;
        }
        else if (Name == "config")
        {
            // the plugin's own window (modal: it runs a nested loop, the next steps run inside it)
            const int Type = Arg == "video" ? PLUGIN_TYPE_VIDEO : Arg == "audio" ? PLUGIN_TYPE_AUDIO : Arg == "rsp" ? PLUGIN_TYPE_RSP : PLUGIN_TYPE_CONTROLLER;
            QTimer::singleShot(0, this, [this, Type]() { g_Plugins->ConfigPlugin(reinterpret_cast<void *>(Window->winId()), (PLUGIN_TYPE)Type); });
            Delay = 700;
        }
        else if (Name == "shotwin" || Name == "closewin" || Name == "tabwin" || Name == "clickwin" || Name == "hoverwin" || Name == "keywin")
        {
            // a top-level window by its object name: shotwin:<object>:<file>, closewin:<object>, tabwin:<object>:<n>,
            // clickwin:<object>:<child object name>
            QWidget * Found = nullptr;
            for (QWidget * W : QApplication::topLevelWidgets())
            {
                if (W->objectName() == Arg.section(':', 0, 0) && W->isVisible())
                {
                    Found = W;
                }
            }
            if (Found == nullptr)
            {
                printf("window %s not found\n", Arg.section(':', 0, 0).toUtf8().constData());
            }
            else if (Name == "shotwin")
            {
                QDir().mkpath(Dir);
                const QString File = Dir + "/" + Arg.section(':', 1) + ".png";
                printf("shot %s %s\n", File.toUtf8().constData(), Found->grab().save(File) ? "ok" : "FAILED");
            }
            else if (Name == "closewin")
            {
                if (QDialog * D = qobject_cast<QDialog *>(Found)) D->reject(); else Found->close();
            }
            else if (Name == "tabwin")
            {
                for (QTabWidget * Tabs : Found->findChildren<QTabWidget *>())
                {
                    if (Tabs->isVisible())
                    {
                        Tabs->setCurrentIndex(Arg.section(':', 1).toInt());
                    }
                }
            }
            else if (Name == "hoverwin")
            {
                // hoverwin:<object>:<child object name>:<row>: the real pointer over the child whose property "row" is that number
                for (QWidget * C : Found->findChildren<QWidget *>(Arg.section(':', 1, 1)))
                {
                    if (C->isVisible() && C->property("row").toInt() == Arg.section(':', 2, 2).toInt())
                    {
                        QCursor::setPos(C->mapToGlobal(C->rect().center()));
                    }
                }
                Delay = 400;
            }
            else if (Name == "keywin")
            {
                // keywin:<object>:<Qt::Key number>:<down|up>[:kp]: a key event for the application (the filters see it)
                const int Key = Arg.section(':', 1, 1).toInt();
                const bool Down = Arg.section(':', 2, 2) != "up";
                QKeyEvent Event(Down ? QEvent::KeyPress : QEvent::KeyRelease, Key, Arg.section(':', 3, 3) == "kp" ? Qt::KeypadModifier : Qt::NoModifier);
                QApplication::sendEvent(Found, &Event);
                Delay = 200;
            }
            else
            {
                // clickwin:<object>:<child object name>[:<row>]
                QPushButton * B = nullptr;
                for (QPushButton * C : Found->findChildren<QPushButton *>(Arg.section(':', 1, 1)))
                {
                    if (B == nullptr && C->isVisible() && (Arg.section(':', 2, 2).isEmpty() || C->property("row").toInt() == Arg.section(':', 2, 2).toInt()))
                    {
                        B = C;
                    }
                }
                printf("clickwin %s: %s\n", Arg.toUtf8().constData(), B != nullptr ? (B->isEnabled() ? "clicked" : "disabled") : "not found");
                if (B != nullptr)
                {
                    B->click();
                }
                Delay = 300;
            }
            fflush(stdout);
        }
        else if (Name == "lang")
        {
            // lang:es | en | auto: the whole application changes language, as with Settings > General
            ApplyLanguage(Arg);
            Delay = 600;
        }
        else if (Name == "missing")
        {
            // the texts of the catalog that were asked for in Spanish and have no translation
            const QStringList Missing = MissingTranslations();
            printf("missing %d\n", (int)Missing.size());
            for (const QString & M : Missing)
            {
                printf("missing: %s\n", M.toUtf8().constData());
            }
            fflush(stdout);
        }
        else if (Name == "debug")
        {
            // debug:<view>: opens a window of the debugger (commands, memory, search, dump, symbols, stack, stacktrace, tlb, exceptions, dmalog, cpulog, scripts)
            if (CDebuggerUI * Debugger = static_cast<CDebuggerUI *>(g_Debugger))
            {
                if (Arg == "commands") Debugger->OpenCommandWindow();
                else if (Arg == "memory") Debugger->OpenMemoryWindow();
                else if (Arg == "search") Debugger->OpenMemorySearch();
                else if (Arg == "dump") Debugger->OpenMemoryDump();
                else if (Arg == "symbols") Debugger->OpenSymbolsWindow();
                else if (Arg == "stack") Debugger->OpenStackViewWindow();
                else if (Arg == "stacktrace") Debugger->OpenStackTraceWindow();
                else if (Arg == "tlb") Debugger->OpenTLBWindow();
                else if (Arg == "exceptions") Debugger->OpenExcBreakpointsWindow();
                else if (Arg == "dmalog") Debugger->OpenDMALogWindow();
                else if (Arg == "cpulog") Debugger->OpenCPULogWindow();
                else if (Arg == "scripts") Debugger->OpenScriptsWindow();
            }
            Delay = 500;
        }
        else if (Name == "hover")
        {
            // hover:<object name of an item view>:<row>: the real pointer over that row (it is what paints the hover state)
            for (QAbstractItemView * V : QApplication::activeWindow() != nullptr ? QApplication::activeWindow()->findChildren<QAbstractItemView *>(Arg.section(':', 0, 0)) : QList<QAbstractItemView *>())
            {
                const QModelIndex I = V->model()->index(Arg.section(':', 1).toInt(), 0);
                QCursor::setPos(V->viewport()->mapToGlobal(V->visualRect(I).center()));
            }
            Delay = 400;
        }
        else if (Name == "closedlg")
        {
            delete Dialog;
            Dialog = nullptr;
        }
        else if (Name == "quit")
        {
            Window->close();
            QApplication::quit();
            return;
        }
        QTimer::singleShot(Delay, this, &Runner::Step);
    }
};
} // namespace

void RunShotScriptFromEnvironment(MainWindow * Window)
{
    const QString Dir = qEnvironmentVariable("VORTEX_SHOTS");
    if (Dir.isEmpty())
    {
        return;
    }
    Runner * R = new Runner;
    R->setParent(Window);
    R->Window = Window;
    R->Dir = Dir;
    // question boxes of the core (the debugger asks about the recompiler) are answered: the tour must go on
    QTimer * Dismiss = new QTimer(R);
    QObject::connect(Dismiss, &QTimer::timeout, R, []() {
        for (QWidget * W : QApplication::topLevelWidgets())
        {
            if (QMessageBox * Box = qobject_cast<QMessageBox *>(W))
            {
                if (Box->isVisible())
                {
                    Box->accept();
                }
            }
        }
    });
    Dismiss->start(300);
    R->Steps = qEnvironmentVariable("VORTEX_SHOTS_STEPS", "shot:library,quit").split(',', Qt::SkipEmptyParts);
    QTimer::singleShot(1500, R, &Runner::Step);
}
} // namespace Vortex
