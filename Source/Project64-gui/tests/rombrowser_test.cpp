// Regression test for the Qt ROM browser (Source/Project64-gui/RomBrowser.cpp):
// scans a directory through the core's CRomList, checks the default columns,
// the displayed data, multi-key sort persistence, column width persistence and
// the activation signal. Offscreen Qt, isolated XDG dirs. The ROM directory is
// given on the command line (it must contain one N64 ROM).
#include "../RomBrowser.h"
#include "../RomInformation.h"

#include <Common/path.h>
#include <Project64-cli/LinuxNotification.h>
#include <Project64-core/AppInit.h>
#include <Project64-core/Multilanguage.h>
#include <Project64-core/Settings.h>

#include <QApplication>
#include <QElapsedTimer>
#include <QDialog>
#include <QHeaderView>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMenu>
#include <QTimer>
#include <QSignalSpy>
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

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        printf("usage: %s <rom directory>\n", argv[0]);
        return 2;
    }
    QApplication App(argc, argv);
    static CLinuxNotification Notify;
    const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
    if (!AppInit(&Notify, Base.c_str(), 1, argv))
    {
        return 2;
    }
    RegisterUISettings();

    std::string Dir = argv[1];
    if (Dir.back() != '/')
    {
        Dir += '/';
    }
    g_Settings->SaveString(RomList_GameDir, Dir);

    static QStringList InfoTexts;
    struct Grab
    {
        static void Run()
        {
            for (QWidget * W : QApplication::topLevelWidgets())
            {
                QDialog * D = qobject_cast<QDialog *>(W);
                if (D != nullptr && D->isVisible())
                {
                    for (QLineEdit * E : D->findChildren<QLineEdit *>())
                    {
                        InfoTexts << E->text();
                    }
                    D->accept();
                    return;
                }
            }
            QTimer::singleShot(100, Run);
        }
    };
    {
        QtRomBrowser Browser;
        // Default columns of GetFieldInfo: Name (pos 0) and Status (pos 1).
        CHECK(Browser.columnCount() == 2);
        CHECK(Browser.horizontalHeaderItem(0)->text() == QString::fromUtf8(GS(RB_NAME)));

        QSignalSpy Activated(&Browser, &QtRomBrowser::RomActivated);
        Browser.RefreshRomList();
        QElapsedTimer Timer;
        Timer.start();
        while (Browser.rowCount() == 0 && Timer.elapsed() < 30000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        // Let the refresh thread finish its final RomListLoaded callback.
        for (int i = 0; i < 20; i++)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        CHECK(Browser.rowCount() >= 1);
        if (Browser.rowCount() >= 1)
        {
            printf("row 0: '%s' | '%s'\n", Browser.item(0, 0)->text().toUtf8().constData(), Browser.item(0, 1)->text().toUtf8().constData());
            CHECK(!Browser.item(0, 0)->text().isEmpty());
        }

        // Column click sorts by that field and stores it as the first sort key.
        CHECK(UISettingsLoadStringIndex(RomBrowser_SortFieldIndex, 0).empty());
        Q_EMIT Browser.horizontalHeader()->sectionClicked(1);
        CHECK(UISettingsLoadStringIndex(RomBrowser_SortFieldIndex, 0) == "Status");
        CHECK(UISettingsLoadBoolIndex(RomBrowser_SortAscendingIndex, 0));
        Q_EMIT Browser.horizontalHeader()->sectionClicked(1);
        CHECK(!UISettingsLoadBoolIndex(RomBrowser_SortAscendingIndex, 0));
        Q_EMIT Browser.horizontalHeader()->sectionClicked(0);
        CHECK(UISettingsLoadStringIndex(RomBrowser_SortFieldIndex, 0) == "Name");
        CHECK(UISettingsLoadStringIndex(RomBrowser_SortFieldIndex, 1) == "Status");

        // Resizing a column persists its width.
        Browser.horizontalHeader()->resizeSection(0, 333);
        uint32_t Width = 0;
        CHECK(UISettingsLoadDwordIndex(RomBrowser_WidthIndex, 3 /* RB_Name */, Width) && Width == 333);

        // The pop-up menu (RomList_PopupMenu): nothing selected, a ROM, basic mode, cheats and
        // enhancements
        {
            auto Texts = [&]() {
                QMenu Menu;
                Browser.FillContextMenu(Menu);
                QStringList List;
                for (QAction * Action : Menu.actions())
                {
                    List << (Action->isSeparator() ? QString("-") : Action->text());
                }
                return List;
            };
            const QString Play = QString::fromUtf8(GS(POPUP_PLAY)), PlayDisk = QString::fromUtf8(GS(POPUP_PLAYDISK)), Refresh = QString::fromUtf8(GS(MENU_REFRESH)),
                          Choose = QString::fromUtf8(GS(MENU_CHOOSE_ROM)), Info = QString::fromUtf8(GS(POPUP_INFO)), Settings = QString::fromUtf8(GS(POPUP_SETTINGS)),
                          Cheats = QString::fromUtf8(GS(POPUP_CHEATS)), Enhancements = QString::fromUtf8(GS(POPUP_ENHANCEMENTS));
            g_Settings->SaveBool(UserInterface_BasicMode, false);
            g_Settings->SaveBool(Setting_RememberCheats, false);
            g_Settings->SaveBool(Setting_Enhancement, false);
            Browser.clearSelection();
            Browser.setCurrentCell(-1, -1);
            if (Browser.SelectedRom().isEmpty())
            {
                CHECK(Texts() == (QStringList{Refresh, Choose}));
            }
            Browser.selectRow(0);
            CHECK(!Browser.SelectedRom().isEmpty());
            CHECK(Texts() == (QStringList{Play, PlayDisk, "-", Refresh, Choose, "-", Info, "-", Settings}));
            g_Settings->SaveBool(Setting_RememberCheats, true);
            g_Settings->SaveBool(Setting_Enhancement, true);
            CHECK(Texts() == (QStringList{Play, PlayDisk, "-", Refresh, Choose, "-", Info, "-", Settings, Cheats, Enhancements}));
            g_Settings->SaveBool(UserInterface_BasicMode, true);
            CHECK(Texts() == (QStringList{Play, PlayDisk, "-", Refresh, Choose, "-", Info}));
            g_Settings->SaveBool(Setting_RememberCheats, false);
            g_Settings->SaveBool(UserInterface_BasicMode, false);
            g_Settings->SaveBool(Setting_Enhancement, false);
            // the entries emit what the main window handles
            QSignalSpy Disk(&Browser, &QtRomBrowser::PlayWithDiskRequested), Refreshed(&Browser, &QtRomBrowser::RefreshRequested),
                Directory(&Browser, &QtRomBrowser::ChooseDirectoryRequested), GameSettings(&Browser, &QtRomBrowser::EditGameSettingsRequested);
            QMenu Menu;
            Browser.FillContextMenu(Menu);
            for (QAction * Action : Menu.actions())
            {
                if (!Action->isSeparator() && (Action->text() == PlayDisk || Action->text() == Refresh || Action->text() == Choose || Action->text() == Settings))
                {
                    Action->trigger();
                }
            }
            CHECK(Disk.count() == 1 && Refreshed.count() == 1 && Directory.count() == 1 && GameSettings.count() == 1);
            CHECK(Disk.first().first().toString().startsWith(QString::fromStdString(Dir)));
        }

        // The ROM directory is watched: a ROM that appears in it (or in a subdirectory, with the
        // recursive setting) is in the list without asking for a refresh, and a check when the
        // watch starts finds what changed while it was off
        {
            Browser.WatchStart();
            for (int i = 0; i < 40 && Browser.Checking(); i++)
            {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            }
            const int Rows = Browser.rowCount();
            const QString Copy = QString::fromStdString(Dir) + "second.z64";
            CHECK(QFile::copy(QString::fromStdString(Dir) + "sm64.z64", Copy));
            QElapsedTimer Watch;
            Watch.start();
            while (Browser.rowCount() < Rows + 1 && Watch.elapsed() < 20000)
            {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            }
            CHECK(Browser.rowCount() == Rows + 1);
            printf("watch: a new ROM showed up after %lld ms\n", (long long)Watch.elapsed());
            QFile::remove(Copy);
            Watch.restart();
            // (the list is emptied and filled again: what is waited for is the same number of rows
            // as before, not fewer)
            while ((Browser.rowCount() != Rows || Watch.elapsed() < 1500) && Watch.elapsed() < 20000)
            {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            }
            CHECK(Browser.rowCount() == Rows);
            // stopped: nothing follows
            Browser.WatchStop();
            CHECK(QFile::copy(QString::fromStdString(Dir) + "sm64.z64", Copy));
            Watch.restart();
            while (Watch.elapsed() < 1500)
            {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            }
            CHECK(Browser.rowCount() == Rows);
            // started again it finds the change that happened meanwhile
            Browser.WatchStart();
            Watch.restart();
            while (Browser.rowCount() < Rows + 1 && Watch.elapsed() < 20000)
            {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            }
            CHECK(Browser.rowCount() == Rows + 1);
            Browser.WatchStop();
            QFile::remove(Copy);
        }

        // Return plays the selected ROM (NM_RETURN); a click on a ROM stops the last ROM from being
        // highlighted again (NM_CLICK / m_AllowSelectionLastRom) until the browser is shown again
        if (Browser.rowCount() >= 1)
        {
            QSignalSpy Played(&Browser, &QtRomBrowser::RomActivated);
            Browser.selectRow(0);
            Browser.setCurrentCell(0, 0);
            QKeyEvent Return(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(&Browser, &Return);
            CHECK(Played.count() == 1);
            Browser.clearSelection();
            Browser.setCurrentCell(-1, -1);
            Browser.ShowingBrowser(); // the last ROM played is selected again
            // (the recent ROM list is empty in this test: nothing to select)
            const QRect Cell = Browser.visualItemRect(Browser.item(0, 0));
            QMouseEvent Press(QEvent::MouseButtonPress, QPointF(Cell.center()), Browser.viewport()->mapToGlobal(QPointF(Cell.center())), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(Browser.viewport(), &Press);
            CHECK(!Browser.SelectedRom().isEmpty());
            Browser.clearSelection();
            Browser.HighLightLastRom();
            CHECK(Browser.SelectedRom().isEmpty()); // after a click it is left alone
            UISettingsSaveStringIndex(File_RecentGameFileIndex, 0, Browser.item(0, 0) != nullptr ? std::string(Dir + "sm64.z64") : std::string());
            Browser.ShowingBrowser(); // shown again: highlighted again
            CHECK(!Browser.SelectedRom().isEmpty());
        }

        // Double click on a row emits the ROM path.
        if (Browser.rowCount() >= 1)
        {
            const int Before = Activated.count();
            Q_EMIT Browser.cellDoubleClicked(0, 0);
            CHECK(Activated.count() == Before + 1);
            if (Activated.count() == Before + 1)
            {
                CHECK(Activated.last().first().toString().startsWith(QString::fromStdString(Dir)));
            }
        }
    }

    // ROM Information dialog: same fields and conversions as RomInformation.cpp.
    {
        std::string RomPath;
        QSignalSpy Dummy(QApplication::instance(), &QObject::destroyed);
        QTimer::singleShot(200, Grab::Run);
        std::string Path = Dir + "sm64.z64";
        CHECK(ShowRomInformation(nullptr, QString::fromStdString(Path)));
        CHECK(!InfoTexts.isEmpty());
        CHECK(InfoTexts.contains("SUPER MARIO 64"));
        CHECK(InfoTexts.contains("64.z64") || InfoTexts.contains("sm64.z64"));
        CHECK(InfoTexts.contains("N64 Cartridge"));
        CHECK(InfoTexts.contains("America"));
        CHECK(InfoTexts.contains("0x635A2BFF") && InfoTexts.contains("0x8B022326"));
        CHECK(InfoTexts.contains("CIC-NUS-6102"));
    }

    AppCleanup();
    printf(g_Failures == 0 ? "PASS\n" : "FAILED\n");
    return g_Failures == 0 ? 0 : 1;
}
