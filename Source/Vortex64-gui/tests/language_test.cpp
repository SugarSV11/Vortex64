// The languages of Vortex64: exactly Spanish and English, both complete, applied at once, kept between runs, and no text of the
// other language left in any window of the frontend. Offscreen Qt, isolated XDG directories:
//
//   language-test                       the test
//   language-test --language-of <file>  (child mode) prints the language a run with that store would use
#include "ui_test_env.h"
#include "../Debugger/DebuggerUI.h"
#include "../Dialogs/AboutDialog.h"
#include "../Dialogs/CheatDialog.h"
#include "../Dialogs/EnhancementDialog.h"
#include "../Dialogs/LogOptionsDialog.h"
#include "../RomInformation.h"
#include "../Vortex/Language.h"
#include "../Vortex/Sidebar.h"

#include <Vortex64-core/Multilanguage.h>
#include <Vortex64-core/N64System/SystemGlobals.h>

#include <QAbstractButton>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QCoreApplication>
#include <QComboBox>
#include <QGroupBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QTabBar>
#include <QTimer>

using namespace Vortex;

// "&File" -> "File", "Open ROM..." -> "Open ROM"
static QString Plain(QString Text)
{
    Text.remove(QLatin1Char('&'));
    Text.remove(QLatin1String("..."));
    Text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    return Text.trimmed();
}

// The language file of the core ("#100#  "&File"") by number
static QHash<int, QString> LoadLanguageFile(const QString & Path)
{
    QHash<int, QString> Result;
    QFile File(Path);
    if (!File.open(QIODevice::ReadOnly))
    {
        return Result;
    }
    const QRegularExpression Line(QStringLiteral("^#(\\d+)\\s*#\\s*\"(.*)\"\\s*(//.*)?$"));
    for (const QByteArray & Raw : File.readAll().split('\n'))
    {
        const QRegularExpressionMatch M = Line.match(QString::fromUtf8(Raw).trimmed());
        if (M.hasMatch())
        {
            QString Text = M.captured(2);
            Text.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
            Result.insert(M.captured(1).toInt(), Text);
        }
    }
    return Result;
}

// Every text a person can read in the windows that are open now
static QHash<QString, QString> g_Where; // text -> the widget that shows it, for the report

static QSet<QString> VisibleTexts()
{
    QSet<QString> Texts;
    const QWidget * Current = nullptr;
    auto Add = [&](const QString & Text) {
        const QString P = Plain(Text);
        if (!P.isEmpty())
        {
            Texts.insert(P);
            if (Current != nullptr)
            {
                g_Where.insert(P, QString::fromLatin1(Current->metaObject()->className()) + "#" + Current->objectName() + " in " + (Current->window() != nullptr ? Current->window()->objectName() + "/" + QString::fromLatin1(Current->window()->metaObject()->className()) : QString()));
            }
        }
    };
    for (QWidget * Widget : QApplication::allWidgets())
    {
        if (!Widget->isVisible() && qobject_cast<QMenu *>(Widget) == nullptr)
        {
            continue;
        }
        Current = Widget;
        if (Widget->isWindow())
        {
            Add(Widget->windowTitle());
        }
        if (QLabel * L = qobject_cast<QLabel *>(Widget))
        {
            Add(L->text());
            Add(L->toolTip());
        }
        else if (QAbstractButton * B = qobject_cast<QAbstractButton *>(Widget))
        {
            Add(B->text());
            Add(B->toolTip());
        }
        else if (QGroupBox * G = qobject_cast<QGroupBox *>(Widget))
        {
            Add(G->title());
        }
        else if (QTabBar * T = qobject_cast<QTabBar *>(Widget))
        {
            for (int i = 0; i < T->count(); i++)
            {
                Add(T->tabText(i));
            }
        }
        else if (QMenuBar * M = qobject_cast<QMenuBar *>(Widget))
        {
            // the menus of the bar, with their submenus
            std::function<void(const QList<QAction *> &)> Walk = [&](const QList<QAction *> & Actions) {
                for (QAction * A : Actions)
                {
                    Add(A->text());
                    if (A->menu() != nullptr)
                    {
                        Walk(A->menu()->actions());
                    }
                }
            };
            Walk(M->actions());
        }
        else if (QComboBox * C = qobject_cast<QComboBox *>(Widget))
        {
            for (int i = 0; i < C->count(); i++)
            {
                Add(C->itemText(i));
            }
        }
    }
    return Texts;
}

static void CloseAbout()
{
    for (QWidget * W : QApplication::topLevelWidgets())
    {
        if (W->objectName() == "VxAbout" && W->isVisible())
        {
            if (QAbstractButton * B = W->findChild<QAbstractButton *>("VxAboutClose"))
            {
                B->click();
            }
        }
    }
}

// A modal window: opened, its texts read from inside its own event loop (a timer), closed
static QSet<QString> ModalTexts(const std::function<void()> & Show)
{
    QSet<QString> Result;
    QTimer Poll;
    int Tries = 0;
    QObject::connect(&Poll, &QTimer::timeout, [&]() {
        QWidget * Modal = QApplication::activeModalWidget();
        if (Modal != nullptr && Modal->isVisible())
        {
            Result.unite(VisibleTexts());
            Modal->close();
            if (Modal->isVisible()) // (a message box with nothing to say close to)
            {
                Modal->hide();
            }
        }
        else if (++Tries > 100)
        {
            QApplication::exit(1);
        }
    });
    Poll.start(150);
    Show();
    return Result;
}

static const QStringList g_DebugViews = {"commands", "memory", "search", "dump", "symbols", "stack", "stacktrace", "tlb", "exceptions", "dmalog", "cpulog", "scripts"};

// The windows of Vortex64 with everything that can be opened without a game
static void OpenEverything(UiTestApp & App)
{
    for (const QString & Page : {QString("library"), QString("recent"), QString("favorites"), QString("disks")})
    {
        App.Window->LibraryShell()->ShowPage(Page);
        QApplication::processEvents();
    }
    App.Window->LibraryShell()->ShowPage("library");
    if (CDebuggerUI * Debugger = static_cast<CDebuggerUI *>(g_Debugger))
    {
        Debugger->OpenCommandWindow();
        Debugger->OpenMemoryWindow();
        Debugger->OpenMemorySearch();
        Debugger->OpenMemoryDump();
        Debugger->OpenSymbolsWindow();
        Debugger->OpenStackViewWindow();
        Debugger->OpenStackTraceWindow();
        Debugger->OpenTLBWindow();
        Debugger->OpenExcBreakpointsWindow();
        Debugger->OpenDMALogWindow();
        Debugger->OpenCPULogWindow();
        Debugger->OpenScriptsWindow();
    }
    QApplication::processEvents();
}

int main(int argc, char ** argv)
{
    setvbuf(stdout, nullptr, _IOLBF, 0);
    if (argc >= 3 && QByteArray(argv[1]) == "--language-of")
    {
        QCoreApplication App(argc, argv);
        Store::SetPath(QString::fromLocal8Bit(argv[2]));
        printf("%s\n", LanguageCode().toUtf8().constData());
        return 0;
    }

    QApplication Qt(argc, argv);
    UiTestApp App;
    CHECK(App.Start(argv[0]));
    if (App.Window == nullptr || App.Window->LibraryShell() == nullptr)
    {
        printf("FAILED\n");
        return 2;
    }
    MainWindow & Window = *App.Window;
    Store & Store = Store::Instance();
    RecordTextPairs(true);

    // ---- exactly two languages, named in themselves, and the selector offers those
    CHECK(SupportedLanguages() == QStringList({"es", "en"}));
    CHECK(LanguageName("es") == QString::fromUtf8("Español") && LanguageName("en") == "English");
    ApplyLanguage("en");
    {
        SettingsDialog Dialog(false, &Window);
        QComboBox * Combo = Dialog.findChild<QComboBox *>("VxLanguage");
        CHECK(Combo != nullptr);
        if (Combo != nullptr)
        {
            CHECK(Combo->count() == 2);
            CHECK(Combo->itemData(0).toString() == "es" && Combo->itemData(1).toString() == "en");
            CHECK(Combo->itemText(0) == QString::fromUtf8("Español") && Combo->itemText(1) == "English");
            CHECK(Combo->currentData().toString() == "en");
        }
    }

    // ---- English: the library, the menus, the status text
    ApplyLanguage("en");
    CHECK(LanguageCode() == "en");
    {
        Vortex::Shell * Shell = Window.LibraryShell();
        CHECK(Shell->SidebarWidget()->Button("library")->text().contains("Library"));
        CHECK(Shell->SidebarWidget()->Button("disks")->text().contains("64DD"));
        QStringList Menus;
        for (QAction * A : Window.menuBar()->actions())
        {
            Menus << Plain(A->text());
        }
        CHECK(Menus.contains("File"));
        CHECK(!Menus.contains("Archivo"));
    }

    // ---- English -> Spanish: at once, the shell is built again with what was there
    {
        Vortex::Shell * Before = Window.LibraryShell();
        Before->SearchBox()->setText("mario");
        QApplication::processEvents();
        const QString Page = Before->CurrentPage();
        const QString Selected = Before->SelectedRom();
        QPointer<Vortex::Shell> Old(Before);
        ApplyLanguage("es");
        QApplication::processEvents();
        Vortex::Shell * Shell = Window.LibraryShell();
        CHECK(LanguageCode() == "es");
        CHECK(Shell != nullptr && Old.isNull()); // (the old one is gone, not left behind with the old texts)
        CHECK(Shell->CurrentPage() == Page);
        CHECK(Shell->SearchBox()->text() == "mario");
        CHECK(Shell->SelectedRom() == Selected);
        CHECK(Shell->Model()->Count() == 5);
        CHECK(Shell->SidebarWidget()->Button("library")->text().contains("Biblioteca"));
        CHECK(Shell->SidebarWidget()->Button("recent")->text().contains("Recientes"));
        CHECK(Shell->SidebarWidget()->Button("favorites")->text().contains("Favoritos"));
        QStringList Menus;
        for (QAction * A : Window.menuBar()->actions())
        {
            Menus << Plain(A->text());
        }
        CHECK(Menus.contains("Archivo"));
        CHECK(!Menus.contains("File"));
        QLabel * Name = Shell->findChild<QLabel *>("VxDetailName");
        CHECK(Name != nullptr);
        QAbstractButton * Start = Shell->findChild<QAbstractButton *>("VxStart");
        CHECK(Start != nullptr && Start->text() == "Iniciar");
        Shell->SearchBox()->setText("");
        // the core follows: its language file is the Spanish one
        bool CoreSpanish = false;
        for (LanguageFile & File : g_Lang->GetLangList())
        {
            if (g_Lang->IsCurrentLang(File))
            {
                CoreSpanish = File.LanguageName == "Español";
            }
        }
        CHECK(CoreSpanish);
    }

    // ---- Spanish -> English and back: it is the same thing both ways
    ApplyLanguage("en");
    QApplication::processEvents();
    CHECK(LanguageCode() == "en");
    CHECK(Window.LibraryShell()->SidebarWidget()->Button("recent")->text().contains("Recent"));
    {
        bool CoreEnglish = false;
        for (LanguageFile & File : g_Lang->GetLangList())
        {
            if (g_Lang->IsCurrentLang(File))
            {
                CoreEnglish = File.LanguageName == "English";
            }
        }
        CHECK(CoreEnglish);
    }
    ApplyLanguage("es");
    QApplication::processEvents();
    CHECK(Window.LibraryShell()->SidebarWidget()->Button("recent")->text().contains("Recientes"));

    // ---- the settings window: categories in the language; Apply with another language reopens it in the new one
    {
        SettingsDialog Dialog(false, &Window);
        Dialog.show();
        const QStringList Ids = Dialog.CategoryIds();
        QListWidget * Nav = Dialog.findChild<QListWidget *>("VxSettingsNav");
        CHECK(Nav != nullptr && Nav->count() == Ids.size());
        if (Nav != nullptr)
        {
            QStringList Titles;
            for (int i = 0; i < Nav->count(); i++)
            {
                Titles << Nav->item(i)->text();
            }
            CHECK(Titles.contains("Directorios") && Titles.contains(QString::fromUtf8("Gráficos")) && Titles.contains("Atajos") && Titles.contains("Avanzado"));
        }
        Dialog.ShowPage("general");
        QComboBox * Combo = Dialog.findChild<QComboBox *>("VxLanguage");
        QAbstractButton * Apply = Dialog.findChild<QAbstractButton *>("VxApply");
        CHECK(Combo != nullptr && Apply != nullptr);
        if (Combo != nullptr && Apply != nullptr)
        {
            Combo->setCurrentIndex(Combo->findData("en"));
            QMetaObject::invokeMethod(Combo, "activated", Q_ARG(int, Combo->currentIndex()));
            CHECK(Apply->isEnabled());
            Apply->click();
            CHECK(Dialog.ReopenRequested());
            CHECK(Dialog.CurrentPageId() == "general");
            CHECK(LanguageCode() == "en");
        }
    }
    {
        SettingsDialog Dialog(false, &Window); // (what MainWindow::OpenSettings builds next)
        QListWidget * Nav = Dialog.findChild<QListWidget *>("VxSettingsNav");
        QStringList Titles;
        for (int i = 0; Nav != nullptr && i < Nav->count(); i++)
        {
            Titles << Nav->item(i)->text();
        }
        CHECK(Titles.contains("Directories") && Titles.contains("Graphics") && Titles.contains("Shortcuts") && Titles.contains("Advanced"));
    }

    // ---- what is written is what the next run finds (a process of its own, with another system language each time)
    auto RunLanguage = [&](const QString & Locale, const QString & StoreFile = QString()) {
        QProcess Child;
        QProcessEnvironment Env = QProcessEnvironment::systemEnvironment();
        for (const char * Key : {"LC_ALL", "LC_MESSAGES", "LANG", "LANGUAGE"})
        {
            Env.remove(Key);
        }
        if (!Locale.isEmpty())
        {
            Env.insert("LC_ALL", Locale);
            Env.insert("LANG", Locale);
        }
        Env.insert("QT_QPA_PLATFORM", "offscreen");
        Child.setProcessEnvironment(Env);
        Child.start(QCoreApplication::applicationFilePath(), {"--language-of", StoreFile.isEmpty() ? App.StorePath : StoreFile});
        Child.waitForFinished(20000);
        return QString::fromUtf8(Child.readAllStandardOutput()).trimmed();
    };
    ApplyLanguage("es");
    Store.Flush();
    {
        QFile File(App.StorePath);
        CHECK(File.open(QIODevice::ReadOnly));
        CHECK(QJsonDocument::fromJson(File.readAll()).object().value("language").toString() == "es");
    }
    CHECK(RunLanguage("en_US.UTF-8") == "es"); // chosen: the system does not matter
    ApplyLanguage("en");
    Store.Flush();
    CHECK(RunLanguage("es_ES.UTF-8") == "en");
    // no preference: Spanish for a Spanish system, English for any other
    const QString AutoStore = App.Work.path() + "/auto.json";
    {
        QFile Source(App.StorePath);
        CHECK(Source.open(QIODevice::ReadOnly));
        QJsonObject Json = QJsonDocument::fromJson(Source.readAll()).object();
        Json.insert("language", "auto");
        QFile Target(AutoStore);
        CHECK(Target.open(QIODevice::WriteOnly));
        Target.write(QJsonDocument(Json).toJson());
    }
    CHECK(RunLanguage("es_ES.UTF-8", AutoStore) == "es");
    CHECK(RunLanguage("es_MX.UTF-8", AutoStore) == "es");
    CHECK(RunLanguage("en_US.UTF-8", AutoStore) == "en");
    CHECK(RunLanguage("de_DE.UTF-8", AutoStore) == "en");
    CHECK(RunLanguage("", AutoStore) == "en");
    // a store without the key is "no preference" too
    {
        QFile Target(App.Work.path() + "/none.json");
        CHECK(Target.open(QIODevice::WriteOnly));
        Target.write("{}");
    }
    CHECK(RunLanguage("es_ES.UTF-8", App.Work.path() + "/none.json") == "es");
    CHECK(RunLanguage("fr_FR.UTF-8", App.Work.path() + "/none.json") == "en");
    ApplyLanguage("es");

    // ---- no text of the other language in any window, in either language
    // (the texts that were asked for in Spanish and are not translated, and the visible texts that are the other language's)
    const QHash<int, QString> CoreEn = LoadLanguageFile(QString::fromStdString(CPath(CPath::MODULE_DIRECTORY).GetDirectory()) + "/Lang/English.pj.Lang");
    const QHash<int, QString> CoreEs = LoadLanguageFile(QString::fromStdString(CPath(CPath::MODULE_DIRECTORY).GetDirectory()) + "/Lang/Spanish.pj.Lang");
    CHECK(CoreEn.size() > 400 && CoreEs.size() > 400);
    for (const QString & Code : {QString("es"), QString("en")})
    {
        ApplyLanguage(Code);
        ClearMissingTranslations();
        OpenEverything(App);
        SettingsDialog Dialog(false, &Window);
        Dialog.show();
        QSet<QString> Texts = VisibleTexts();
        for (const QString & Id : Dialog.CategoryIds())
        {
            Dialog.ShowPage(Id);
            QApplication::processEvents();
            Texts.unite(VisibleTexts());
        }
        QTimer::singleShot(200, &Window, &CloseAbout);
        ShowAboutDialog(&Window);
        // the other windows of the frontend (modal): Log options, ROM information, the dialogs of the cheats
        const QString AnyRom = App.Roms + "/Super Mario 64 (USA).z64";
        Texts.unite(ModalTexts([&]() { LogOptionsDialog D(&Window); D.exec(); }));
        Texts.unite(ModalTexts([&]() { ShowRomInformation(&Window, AnyRom); }));
        Texts.unite(ModalTexts([&]() { ShowCheatDialog(&Window); }));
        Texts.unite(ModalTexts([&]() { ShowEnhancementDialog(&Window); }));
        // the words of the other language: what Tr(spanish, english) and the catalog translate, and the core's files
        QSet<QString> Foreign, Valid{QString::fromUtf8("Español"), QString("English"), QString("Debugger")}; // (the names of the languages are their own; the sidebar says Debugger in both)
        for (const QPair<QString, QString> & Pair : TextPairs())
        {
            const QString Wrong = Code == "es" ? Pair.second : Pair.first;
            const QString Right = Code == "es" ? Pair.first : Pair.second;
            if (Plain(Wrong) != Plain(Right) && Plain(Wrong).size() > 3)
            {
                Foreign.insert(Plain(Wrong));
            }
            Valid.insert(Plain(Right));
        }
        for (auto It = CoreEn.constBegin(); It != CoreEn.constEnd(); ++It)
        {
            const QString Wrong = Plain(Code == "es" ? It.value() : CoreEs.value(It.key()));
            const QString Right = Plain(Code == "es" ? CoreEs.value(It.key()) : It.value());
            if (Wrong != Right && Wrong.size() > 3 && !Wrong.contains('\n'))
            {
                Foreign.insert(Wrong);
            }
            Valid.insert(Right);
        }
        // (a word that is the same in both languages, or right in one place and wrong in another, is not the other language's)
        for (const QString & Word : Valid)
        {
            Foreign.remove(Word);
        }
        int Mixed = 0;
        for (const QString & Text : Texts)
        {
            if (Foreign.contains(Text))
            {
                printf("text of the other language in %s: '%s' (%s)\n", Code.toUtf8().constData(), Text.toUtf8().constData(), g_Where.value(Text).toUtf8().constData());
                Mixed++;
            }
        }
        CHECK(Mixed == 0);
        const QStringList Missing = MissingTranslations();
        for (const QString & M : Missing)
        {
            printf("no translation in the catalog: '%s'\n", M.toUtf8().constData());
        }
        CHECK(Missing.isEmpty());
        printf("%s: %d visible texts checked\n", Code.toUtf8().constData(), (int)Texts.size());
        Dialog.close();
    }

    // ---- a window of the debugger that was open is built again, in the new language, still open
    {
        ApplyLanguage("en");
        if (CDebuggerUI * Debugger = static_cast<CDebuggerUI *>(g_Debugger))
        {
            Debugger->OpenCommandWindow();
        }
        QApplication::processEvents();
        QSet<QString> Before = VisibleTexts();
        CHECK(Before.contains("Skip"));
        ApplyLanguage("es");
        QApplication::processEvents();
        QSet<QString> After = VisibleTexts();
        CHECK(After.contains("Omitir") && !After.contains("Skip"));
        if (!After.contains("Omitir"))
        {
            for (const QString & T : After)
            {
                printf("  after: '%s' (%s)\n", T.toUtf8().constData(), g_Where.value(T).toUtf8().constData());
            }
        }
    }

    printf("%s\n", g_Failures == 0 ? "PASS" : "FAILED");
    Window.close();
    delete App.Debugger;
    return g_Failures == 0 ? 0 : 1;
}
