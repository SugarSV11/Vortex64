// Navigation test of the Vortex64 frontend: the persistent library (scan, cache, search, favourites, history, views),
// the sidebar, the settings window and the check that no button of the windows of Vortex64 is a dead one.
// Offscreen Qt, isolated XDG directories (Scripts/linux/run-offscreen-tests.sh); the games are header-only fixtures:
//
//   vortex-test
#include "../MainMenu.h"
#include "../MainWindow.h"
#include "../Settings/SettingsDialog.h"
#include "../Vortex/Shell.h"
#include "../Vortex/Store.h"
#include "../Dialogs/AboutDialog.h"

#include <Common/path.h>
#include <Vortex64-core/AppInit.h>
#include <Vortex64-core/Settings.h>
#include <Vortex64-ui/Settings/UISettings.h>

#include <QAbstractButton>
#include <QCheckBox>
#include <QPushButton>
#include <QJsonArray>
#include <QMenuBar>
#include <QRadioButton>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaMethod>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QToolButton>
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

// A z64 header (big endian) and zeros / a 64DD disk header
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

struct Peek : QObject
{
    using QObject::receivers;
};

// Every enabled button must do something: a button with nobody listening to its clicks is a decoration
static void CheckButtons(QWidget * Root, const char * What)
{
    for (QAbstractButton * Button : Root->findChildren<QAbstractButton *>())
    {
        if (!Button->isEnabled() || !Button->isVisibleTo(Root))
        {
            continue;
        }
        if (qobject_cast<QCheckBox *>(Button) || qobject_cast<QRadioButton *>(Button))
        {
            continue; // (their state is what they do)
        }
        const bool Listened = static_cast<Peek *>(static_cast<QObject *>(Button))->receivers(SIGNAL(clicked(bool))) > 0 || static_cast<Peek *>(static_cast<QObject *>(Button))->receivers(SIGNAL(toggled(bool))) > 0 || static_cast<Peek *>(static_cast<QObject *>(Button))->receivers(SIGNAL(pressed())) > 0 || (qobject_cast<QToolButton *>(Button) != nullptr && qobject_cast<QToolButton *>(Button)->menu() != nullptr);
        if (!Listened)
        {
            printf("dead button in %s: '%s' (%s)\n", What, Button->text().toUtf8().constData(), Button->objectName().toUtf8().constData());
        }
        CHECK(Listened);
    }
}

int main(int argc, char ** argv)
{
    setvbuf(stdout, nullptr, _IOLBF, 0);
    QApplication App(argc, argv);
    QTemporaryDir Work;
    CHECK(Work.isValid());
    const QString Roms = Work.path() + "/roms";
    const QString More = Work.path() + "/more";
    QDir().mkpath(Roms);
    QDir().mkpath(More);
    MakeRom(Roms + "/Super Mario 64 (USA).z64", "SUPER MARIO 64", 'E', 8 << 20);
    MakeRom(Roms + "/Mario Kart 64 (USA).z64", "MARIOKART64", 'E', 12 << 20);
    MakeRom(Roms + "/Star Fox 64 (Europe).z64", "STARFOX64", 'P', 12 << 20);
    MakeRom(Roms + "/Mario Party 2 (Japan).z64", "MARIOPARTY2", 'J', 32 << 20);
    MakeRom(Roms + "/Wave Race 64 (USA).z64", "WAVE RACE 64", 'E', 8 << 20);
    MakeDisk(Roms + "/F-Zero X Expansion (Japan).ndd");
    MakeDisk(Roms + "/SimCity 64 (Japan).ndd");
    MakeRom(More + "/Banjo-Kazooie (USA).z64", "BANJO-KAZOOIE", 'E', 16 << 20);
    const QString StorePath = Work.path() + "/vortex64/vortex.json";
    Vortex::Store::SetPath(StorePath);

    MainWindow Window;
    const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
    char * Args[] = {argv[0], nullptr};
    if (!AppInit(Window.GetNotify(), Base.c_str(), 1, Args))
    {
        return 2;
    }
    g_Settings->SaveString(RomList_GameDir, (Roms + "/").toStdString());
    g_Settings->SaveBool(UserInterface_BasicMode, false);
    Window.CreateLanguageMenus();
    Window.show();
    Vortex::Shell * Shell = Window.LibraryShell();
    CHECK(Shell != nullptr);
    if (Shell == nullptr)
    {
        return 2;
    }
    Vortex::Store & Store = Vortex::Store::Instance();
    Store.SetLanguage("en");

    // ---- the library is there once the scan ends: 5 games and 2 disks
    CHECK(WaitFor([&]() { return Shell->Model()->Count() == 7 && !Shell->Model()->Scanning(); }, 20000));
    printf("library: %d entries\n", Shell->Model()->Count());
    CHECK(Shell->CurrentPage() == "library");
    CHECK(Shell->Page("library")->Proxy()->rowCount() == 5);
    CHECK(Shell->Disks()->Proxy()->rowCount() == 2);
    CHECK(Shell->Page("favorites")->Proxy()->rowCount() == 0);
    CHECK(Shell->Page("recent")->Proxy()->rowCount() == 0);
    CHECK(Window.RomBrowserShown());
    CHECK(!Window.menuBar()->isVisible()); // the library has no menu bar
    // an empty list explains itself
    CHECK(Shell->Page("favorites")->Browser()->EmptyShown());

    // ---- a game is selected from the start and the detail strip is real
    CHECK(!Shell->SelectedRom().isEmpty());
    const Vortex::Entry * Selected = Shell->Model()->Find(Shell->SelectedRom());
    CHECK(Selected != nullptr);
    if (Selected != nullptr)
    {
        CHECK(Selected->Size > 0);
        CHECK(Shell->Page("library")->Detail()->Rom() == Selected->Path);
        CHECK(Shell->Page("library")->Detail()->StartButton()->isEnabled());
    }

    // ---- search: in real time, by name
    Shell->SearchBox()->setText("mario");
    QApplication::processEvents();
    CHECK(Shell->Page("library")->Proxy()->rowCount() == 3); // Super Mario 64, Mario Kart 64, Mario Party 2
    Shell->SearchBox()->setText("zzzz");
    QApplication::processEvents();
    CHECK(Shell->Page("library")->Proxy()->rowCount() == 0);
    CHECK(Shell->Page("library")->Browser()->EmptyShown());
    Shell->SearchBox()->setText("");
    QApplication::processEvents();
    CHECK(Shell->Page("library")->Proxy()->rowCount() == 5);

    // ---- view modes persist
    CHECK(Shell->ViewButton("list") != nullptr);
    Shell->ViewButton("list")->click();
    CHECK(Shell->Page("library")->Mode() == "list");
    Shell->ViewButton("compact")->click();
    CHECK(Shell->Page("library")->Mode() == "compact");
    Shell->ViewButton("grid")->click();
    CHECK(Shell->Page("library")->Mode() == "grid");
    Shell->ViewButton("list")->click();

    // ---- favourites: the star, the page, the file
    const QString Mario = Roms + "/Super Mario 64 (USA).z64";
    const QString Kart = Roms + "/Mario Kart 64 (USA).z64";
    Store.SetFavorite(Mario, true);
    Store.SetFavorite(Kart, true);
    QApplication::processEvents();
    CHECK(Shell->Page("favorites")->Proxy()->rowCount() == 2);
    Store.SetFavorite(Kart, false);
    QApplication::processEvents();
    CHECK(Shell->Page("favorites")->Proxy()->rowCount() == 1);

    // ---- history: a game that was played shows in Recents with its time
    Store.SessionStart(Mario);
    QThread::msleep(1100);
    Store.SessionEnd();
    QApplication::processEvents();
    CHECK(Shell->Page("recent")->Proxy()->rowCount() == 1);
    CHECK(Store.Record(Mario).Launches == 1);
    CHECK(Store.Record(Mario).Seconds >= 1);
    Shell->SelectRom(Mario);
    CHECK(Shell->Page("library")->Detail()->Rom() == Mario);

    // ---- the sidebar
    Shell->SidebarWidget()->Button("recent")->click();
    CHECK(Shell->CurrentPage() == "recent");
    Shell->SidebarWidget()->Button("favorites")->click();
    CHECK(Shell->CurrentPage() == "favorites");
    Shell->SidebarWidget()->Button("disks")->click();
    CHECK(Shell->CurrentPage() == "disks");
    CHECK(Shell->Disks()->Proxy()->rowCount() == 2);
    CHECK(!Shell->SelectedRom().isEmpty());
    Shell->SidebarWidget()->Button("library")->click();
    CHECK(Shell->CurrentPage() == "library");

    // ---- more folders: added to the library, scanned, kept in the file
    Window.AddLibraryDirectory(More);
    CHECK(WaitFor([&]() { return Shell->Model()->Count() == 8 && !Shell->Model()->Scanning(); }, 20000));
    CHECK(Store.ExtraDirectories().size() == 1);
    CHECK(Shell->Page("library")->Proxy()->rowCount() == 6);

    // ---- what is written is what a next run finds
    Store.Flush();
    QFile File(StorePath);
    CHECK(File.open(QIODevice::ReadOnly));
    const QJsonObject Json = QJsonDocument::fromJson(File.readAll()).object();
    CHECK(Json.value("favorites").toArray().size() == 1);
    CHECK(Json.value("plays").toObject().contains(Mario));
    CHECK(Json.value("directories").toArray().size() == 1);
    CHECK(Json.value("view.library").toString() == "list");
    CHECK(Json.value("page").toString() == "library");

    // ---- the settings window: its categories, and each one opens
    {
        SettingsDialog Dialog(false, &Window);
        Dialog.show();
        const QStringList Ids = Dialog.CategoryIds();
        CHECK(Ids.contains("general") && Ids.contains("directories") && Ids.contains("graphics") && Ids.contains("audio") && Ids.contains("input") && Ids.contains("rsp") && Ids.contains("plugins"));
        for (const QString & Id : Ids)
        {
            Dialog.ShowPage(Id);
            QApplication::processEvents();
            CHECK(Dialog.CurrentCategory() == Id);
            CheckButtons(&Dialog, Id.toUtf8().constData());
        }
        Dialog.close();
    }

    // ---- no dead buttons in the library window
    for (const QString & Page : {QString("library"), QString("recent"), QString("favorites"), QString("disks")})
    {
        Shell->ShowPage(Page);
        QApplication::processEvents();
        CheckButtons(Shell, Page.toUtf8().constData());
    }

    // ---- the game ended... a window title that is Vortex64's
    CHECK(Window.windowTitle() == "Vortex64");

    printf("%s\n", g_Failures == 0 ? "PASS" : "FAILED");
    Window.close();
    return g_Failures == 0 ? 0 : 1;
}
