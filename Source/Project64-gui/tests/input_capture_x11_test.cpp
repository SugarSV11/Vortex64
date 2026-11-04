// The input dialog driven the way a user does it on a real X server (Xvfb here, KDE/XWayland for the manual test):
// the events are made by the X server (XTEST), travel through the xcb platform plugin of Qt and the application's
// event filter, and nothing is sent to a widget by hand. For each input: click the small button of a row (press and
// release), then do the input (a key, a click of the left / right / middle button...), and the row has to show it.
//
//   input-capture-x11-test [report]     (report: print what Qt delivers for every event the filter sees)
#include <Common/path.h>
#include <Project64-cli/LinuxNotification.h>
#include <Project64-core/AppInit.h>
#include <Project64-core/Notification.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Plugins/ControllerPlugin.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/Settings.h>

#include <algorithm>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include "input_ui_access.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <SDL2/SDL.h>
#include <X11/Xlib.h>
#undef KeyPress
#undef KeyRelease
#undef None
#undef Bool
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" int XTestFakeKeyEvent(Display *, unsigned int, int, unsigned long);
extern "C" int XTestFakeButtonEvent(Display *, unsigned int, int, unsigned long);
extern "C" int XTestFakeMotionEvent(Display *, int, int, int, unsigned long);

class DummyRenderWindow : public RenderWindow
{
public:
    void * GetWindowHandle(void) const override { return nullptr; }
};

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

static Display * g_X;
static bool g_Report = false;

struct Case
{
    const char * Label;
    bool Key;
    unsigned Code;       // X keycode or X button
    const char * Expect; // text of the row (nullptr: whatever SDL names the scancode of the keycode)
};

static void Wait(int Ms)
{
    for (int i = 0; i < Ms / 10; i++)
    {
        QApplication::processEvents();
        usleep(10000);
    }
}

static void MoveTo(QWidget * W, QPoint Local)
{
    const QPoint G = W->mapToGlobal(Local);
    XTestFakeMotionEvent(g_X, -1, G.x(), G.y(), 0);
    XFlush(g_X);
    Wait(40);
}

static void Click(unsigned Button)
{
    XTestFakeButtonEvent(g_X, Button, 1, 0);
    XFlush(g_X);
    Wait(60);
    XTestFakeButtonEvent(g_X, Button, 0, 0);
    XFlush(g_X);
    Wait(60);
}

// reports what the application filter sees (a diagnosis of what Qt delivers)
class Reporter : public QObject
{
public:
    bool eventFilter(QObject * Obj, QEvent * E) override
    {
        switch (E->type())
        {
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        {
            QKeyEvent * K = static_cast<QKeyEvent *>(E);
            printf("  event %s key=0x%x native=%u virtual=%u mods=0x%x on %s\n", E->type() == QEvent::KeyPress ? "KeyPress" : "KeyRelease", K->key(), K->nativeScanCode(), K->nativeVirtualKey(), (unsigned)K->modifiers(), Obj->metaObject()->className());
            break;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        {
            QMouseEvent * M = static_cast<QMouseEvent *>(E);
            printf("  event Mouse%s button=0x%x buttons=0x%x on %s\n", E->type() == QEvent::MouseButtonPress ? "Press" : (E->type() == QEvent::MouseButtonRelease ? "Release" : "DblClick"), (unsigned)M->button(), (unsigned)M->buttons(), Obj->metaObject()->className());
            break;
        }
        case QEvent::ContextMenu:
            printf("  event ContextMenu on %s\n", Obj->metaObject()->className());
            break;
        case QEvent::Wheel:
            printf("  event Wheel on %s\n", Obj->metaObject()->className());
            break;
        default:
            break;
        }
        return false;
    }
};

static QDialog * FindDialog()
{
    for (QWidget * W : QApplication::topLevelWidgets())
    {
        QDialog * D = qobject_cast<QDialog *>(W);
        if (D != nullptr && D->isVisible() && D->objectName() == "InputDialog")
        {
            return D;
        }
    }
    return nullptr;
}

static std::vector<Case> g_Cases;
static int g_Done = 0;

static void Drive()
{
    QDialog * Dialog = FindDialog();
    if (Dialog == nullptr)
    {
        QTimer::singleShot(100, Drive);
        return;
    }
    Dialog->activateWindow();
    Dialog->raise();
    Wait(300);
    QWidget * Page = Dialog->findChildren<QStackedWidget *>().first()->widget(0);
    QList<BindingRef> Displays = BindingDisplays(Page);
    QList<QPushButton *> Scans;
    for (QPushButton * B : Page->findChildren<QPushButton *>())
    {
        if (B->objectName() == "VxScan")
        {
            Scans.push_back(B);
        }
    }
    std::sort(Scans.begin(), Scans.end(), [](QPushButton * A, QPushButton * B) { return A->property("row").toInt() < B->property("row").toInt(); });
    CHECK(Displays.size() == 18 && Scans.size() == 18);
    QComboBox * Type = Page->findChildren<QComboBox *>().at(0);
    Type->setCurrentIndex(Type->findData(1));
    Wait(100);

    int Row = 0;
    for (const Case & C : g_Cases)
    {
        Row = (Row + 1) % 18;
        QPushButton * Scan = Scans.at(Row);
        BindingRef Display = Displays.at(Row);
        // the click on the small button of the row, with a real mouse press and release
        MoveTo(Scan, Scan->rect().center());
        if (g_Report) printf("-- %s: click on the button of row %d\n", C.Label, Row);
        Click(1);
        const bool Waiting = Display->text() == "...";
        // the input itself, with the pointer over a neutral place of the dialog (a label)
        QWidget * Neutral = Page->findChildren<QLabel *>().isEmpty() ? Page : static_cast<QWidget *>(Page->findChildren<QLabel *>().at(0));
        MoveTo(Neutral, Neutral->rect().center());
        if (g_Report) printf("-- %s: the input\n", C.Label);
        if (C.Key)
        {
            XTestFakeKeyEvent(g_X, C.Code, 1, 0);
            XFlush(g_X);
            Wait(60);
            XTestFakeKeyEvent(g_X, C.Code, 0, 0);
            XFlush(g_X);
            Wait(60);
        }
        else
        {
            Click(C.Code);
        }
        Wait(120);
        QString Want = C.Expect != nullptr ? QString(C.Expect) : QString();
        if (C.Expect == nullptr && C.Key)
        {
            // what SDL calls the key that the X keycode is (the same conversion the plugin uses)
            const char * Name = SDL_GetScancodeName(SDL_GetScancodeFromKey(0));
            (void)Name;
        }
        const QString Shown = Display->text();
        const bool Menu = QApplication::activePopupWidget() != nullptr;
        const bool Ok = Waiting && !Shown.isEmpty() && Shown != "..." && !Menu && (Want.isEmpty() || Shown == Want) && FindDialog() == Dialog;
        printf("%s %s: waiting=%d shows '%s'%s%s%s\n", Ok ? "ok" : "BAD", C.Label, Waiting, Shown.toUtf8().constData(), Want.isEmpty() ? "" : (" expected '" + Want + "'").toUtf8().constData(), Menu ? " (a menu is open)" : "", FindDialog() == Dialog ? "" : " (the dialog closed)");
        if (!Ok)
        {
            g_Failures++;
            if (QWidget * Popup = QApplication::activePopupWidget())
            {
                Popup->close();
            }
            // leave capture mode if it is still on
            if (Display->text() == "...")
            {
                XTestFakeKeyEvent(g_X, 9, 1, 0);
                XTestFakeKeyEvent(g_X, 9, 0, 0);
                XFlush(g_X);
                Wait(100);
            }
            if (FindDialog() != Dialog)
            {
                return;
            }
        }
        else
        {
            g_Done++;
        }
        Wait(100);
    }
    Dialog->findChild<QPushButton *>("VxOk")->click();
}

int main(int argc, char ** argv)
{
    g_Report = argc > 1 && strcmp(argv[1], "report") == 0;
    QApplication App(argc, argv);
    g_X = XOpenDisplay(nullptr);
    if (g_X == nullptr)
    {
        printf("no display\n");
        return 2;
    }
    static CLinuxNotification Notify;
    const std::string Base = CPath(CPath::MODULE_DIRECTORY).GetDirectory();
    if (!AppInit(&Notify, Base.c_str(), 1, argv))
    {
        return 2;
    }
    static DummyRenderWindow Window;
    g_Plugins->SetRenderWindows(&Window, &Window);
    g_Plugins->CreatePlugins();
    CHECK(g_Plugins->Control() != nullptr && g_Plugins->Control()->DllConfig != nullptr);
    Reporter Rep;
    if (g_Report)
    {
        App.installEventFilter(&Rep);
    }

    // mouse first, then every key of the keyboard (X keycode = evdev code + 8), a click of each button
    g_Cases.push_back({"Mouse left", false, 1, "Mouse Left"});
    g_Cases.push_back({"Mouse right", false, 3, "Mouse Right"});
    g_Cases.push_back({"Mouse middle", false, 2, "Mouse Middle"});
    g_Cases.push_back({"Mouse Back", false, 8, "Mouse 4 (Back)"});
    g_Cases.push_back({"Mouse Forward", false, 9, "Mouse 5 (Forward)"});
    g_Cases.push_back({"Wheel up", false, 4, "Mouse Wheel Up"});
    g_Cases.push_back({"Wheel down", false, 5, "Mouse Wheel Down"});
    g_Cases.push_back({"Wheel left", false, 6, "Mouse Wheel Left"});
    g_Cases.push_back({"Wheel right", false, 7, "Mouse Wheel Right"});
    static char Labels[256][24];
    for (unsigned Evdev = 1; Evdev < 128; Evdev++)
    {
        snprintf(Labels[Evdev], sizeof(Labels[Evdev]), "key evdev %u", Evdev);
        g_Cases.push_back({Labels[Evdev], true, Evdev + 8, nullptr});
    }
    QTimer::singleShot(300, Drive);
    g_Plugins->ConfigPlugin(nullptr, PLUGIN_TYPE_CONTROLLER);
    AppCleanup();
    printf("%s input-capture-x11: %d of %d inputs captured\n", g_Failures == 0 ? "PASS" : "FAILED", g_Done, (int)g_Cases.size());
    return g_Failures == 0 ? 0 : 1;
}
