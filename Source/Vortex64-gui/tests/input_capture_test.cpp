// Free assignment in the Qt input dialog (Vortex64-input/ConfigQt.cpp): every
// kind of physical input is delivered to the dialog the way Qt delivers it (key
// events with the X keycode, mouse presses, wheel events) while a row waits for
// input, and has to be shown with the right name, survive OK and the reopening
// of the dialog, and be cleared from the context menu of the row.
//
//   input-capture-test write   assigns 18 inputs to the 18 rows of Player 1, clears row 0, OK
//   input-capture-test read    reopens the dialog and checks every row
#include <Common/path.h>
#include <Vortex64-cli/LinuxNotification.h>
#include <Vortex64-core/AppInit.h>
#include <Vortex64-core/Notification.h>
#include <Vortex64-core/N64System/SystemGlobals.h>
#include <Vortex64-core/Plugins/ControllerPlugin.h>
#include <Vortex64-core/Plugins/Plugin.h>
#include <Vortex64-core/Settings.h>

#include <algorithm>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLineEdit>
#include "input_ui_access.h"
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QWheelEvent>
#include <cstdio>
#include <cstring>

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

enum Kind { KEY, KEY_NONATIVE, MOUSE, WHEEL };
struct Case
{
    Kind Type;
    int Key;          // Qt key / Qt mouse button / wheel angle (y)
    unsigned Native;  // X keycode (evdev + 8)
    int AngleX;
    const char * Expect;
};
// 18 rows of the pad: one input of each family per row
static const Case Cases[18] = {
    {KEY, Qt::Key_A, 38, 0, "A"},
    {KEY, Qt::Key_Shift, 50, 0, "Left Shift"},
    {KEY, Qt::Key_Control, 105, 0, "Right Ctrl"},
    {KEY, Qt::Key_Escape, 9, 0, "Escape"},
    {KEY, Qt::Key_Return, 36, 0, "Return"},
    {KEY, Qt::Key_Space, 65, 0, "Space"},
    {KEY, Qt::Key_Tab, 23, 0, "Tab"},
    {KEY, Qt::Key_Up, 111, 0, "Up"},
    {KEY, Qt::Key_1, 87, 0, "Keypad 1"},
    {KEY_NONATIVE, Qt::Key_F8, 0, 0, "F8"},
    {MOUSE, Qt::LeftButton, 0, 0, "Mouse Left"},
    {MOUSE, Qt::MiddleButton, 0, 0, "Mouse Middle"},
    {MOUSE, Qt::RightButton, 0, 0, "Mouse Right"},
    {MOUSE, Qt::BackButton, 0, 0, "Mouse 4 (Back)"},
    {WHEEL, 120, 0, 0, "Mouse Wheel Up"},
    {WHEEL, -120, 0, 0, "Mouse Wheel Down"},
    {WHEEL, 0, 0, 120, "Mouse Wheel Left"},
    {KEY, Qt::Key_Super_L, 133, 0, "Left GUI"},
};

// the second set: the inputs the first one leaves out (right/left modifier pairs, arrows, a letter, a number, keypad,
// the forward and the sixth button of the mouse, the wheel to the right, the right Super); all 18 rows assigned
static const Case CasesB[18] = {
    {KEY, Qt::Key_Shift, 62, 0, "Right Shift"},
    {KEY, Qt::Key_Control, 37, 0, "Left Ctrl"},
    {KEY, Qt::Key_Alt, 64, 0, "Left Alt"},
    {KEY, Qt::Key_Alt, 108, 0, "Right Alt"},
    {KEY, Qt::Key_Down, 116, 0, "Down"},
    {KEY, Qt::Key_Left, 113, 0, "Left"},
    {KEY, Qt::Key_Right, 114, 0, "Right"},
    {KEY, Qt::Key_Z, 52, 0, "Z"},
    {KEY, Qt::Key_5, 14, 0, "5"},
    {KEY, Qt::Key_0, 90, 0, "Keypad 0"},
    {MOUSE, Qt::ForwardButton, 0, 0, "Mouse 5 (Forward)"},
    {MOUSE, Qt::ExtraButton3, 0, 0, "Mouse 6"},
    {WHEEL, 0, 0, -120, "Mouse Wheel Right"},
    {KEY, Qt::Key_Super_R, 134, 0, "Right GUI"},
    {KEY, Qt::Key_Enter, 104, 0, "Keypad Enter"},
    {KEY, Qt::Key_Q, 24, 0, "Q"},
    {KEY, Qt::Key_Shift, 50, 0, "Left Shift"},
    {KEY, Qt::Key_Control, 105, 0, "Right Ctrl"},
};
static const Case * g_Cases = Cases;
static bool g_SetB = false;

static bool g_Write = false;
static bool g_Seen = false;
static int g_Step = 0;

static void Drive()
{
    QDialog * Dialog = nullptr;
    for (QWidget * W : QApplication::topLevelWidgets())
    {
        QDialog * D = qobject_cast<QDialog *>(W);
        if (D != nullptr && D->isVisible() && D->objectName() == "InputDialog")
        {
            Dialog = D;
        }
    }
    if (Dialog == nullptr)
    {
        QTimer::singleShot(100, Drive);
        return;
    }
    g_Seen = true;
    QWidget * Page = Dialog->findChildren<QStackedWidget *>().first()->widget(0);
    QList<BindingRef> Displays = BindingDisplays(Page);
    QList<QPushButton *> All = Page->findChildren<QPushButton *>();
    QList<QPushButton *> Scans;
    for (QPushButton * B : All)
    {
        if (B->objectName() == "VxScan")
        {
            Scans.push_back(B);
        }
    }
    std::sort(Scans.begin(), Scans.end(), [](QPushButton * A, QPushButton * B) { return A->property("row").toInt() < B->property("row").toInt(); });
    CHECK(Displays.size() == 18 && Scans.size() == 18);
    if (Displays.size() != 18 || Scans.size() != 18)
    {
        Dialog->reject();
        return;
    }
    if (!g_Write)
    {
        for (int i = 0; i < 18; i++)
        {
            const QString Want = (i == 0 && !g_SetB) ? QString() : QString(g_Cases[i].Expect);
            if (Displays.at(i)->text() != Want)
            {
                printf("row %d: '%s' expected '%s'\n", i, Displays.at(i)->text().toUtf8().constData(), Want.toUtf8().constData());
            }
            CHECK(Displays.at(i)->text() == Want);
        }
        Dialog->reject();
        return;
    }
    const int Row = g_Step / 2;
    if (g_Step == 0)
    {
        QComboBox * Type = Page->findChildren<QComboBox *>().at(0);
        Type->setCurrentIndex(Type->findData(1)); // a plugged controller: the rows get enabled
    }
    if (Row < 18)
    {
        const Case & C = g_Cases[Row];
        if ((g_Step & 1) == 0)
        {
            Scans.at(Row)->click();
            CHECK(Displays.at(Row)->text() == "...");
            // the event goes to a widget that is NOT the dialog: the filter must take it first
            QWidget * Target = Scans.at(Row);
            if (C.Type == KEY || C.Type == KEY_NONATIVE)
            {
                QKeyEvent Press(QEvent::KeyPress, C.Key, Qt::NoModifier, C.Native, 0, 0, QString());
                QApplication::sendEvent(Target, &Press);
                QKeyEvent Release(QEvent::KeyRelease, C.Key, Qt::NoModifier, C.Native, 0, 0, QString());
                QApplication::sendEvent(Target, &Release);
            }
            else if (C.Type == MOUSE)
            {
                const QPointF P(2, 2);
                QMouseEvent Press(QEvent::MouseButtonPress, P, Target->mapToGlobal(P.toPoint()), (Qt::MouseButton)C.Key, (Qt::MouseButton)C.Key, Qt::NoModifier);
                QApplication::sendEvent(Target, &Press);
                QMouseEvent Release(QEvent::MouseButtonRelease, P, Target->mapToGlobal(P.toPoint()), (Qt::MouseButton)C.Key, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(Target, &Release);
            }
            else
            {
                const QPointF P(2, 2);
                QWheelEvent Wheel(P, Target->mapToGlobal(P.toPoint()), QPoint(), QPoint(C.AngleX, C.Key), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(Target, &Wheel);
            }
        }
        else
        {
            if (Displays.at(Row)->text() != C.Expect)
            {
                printf("row %d: '%s' expected '%s'\n", Row, Displays.at(Row)->text().toUtf8().constData(), C.Expect);
            }
            CHECK(Displays.at(Row)->text() == C.Expect);
        }
        g_Step++;
        QTimer::singleShot(120, Drive);
        return;
    }
    if (Row == 18 && g_SetB)
    {
        Dialog->findChild<QPushButton *>("VxOk")->click();
        return;
    }
    if (Row == 18)
    {
        // clear row 0 from the context menu of its field
        QTimer::singleShot(250, []() {
            QMenu * Menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            CHECK(Menu != nullptr);
            if (Menu != nullptr)
            {
                for (QAction * A : Menu->actions())
                {
                    if (A->text() == "Clear")
                    {
                        CHECK(A->isEnabled());
                        Menu->setActiveAction(A);
                        QKeyEvent Enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                        QApplication::sendEvent(Menu, &Enter);
                    }
                }
            }
        });
        g_Step += 2;
        QTimer::singleShot(900, Drive);
        QMetaObject::invokeMethod(Scans.at(0), [D = Scans.at(0)]() { QContextMenuEvent E(QContextMenuEvent::Mouse, QPoint(3, 3), D->mapToGlobal(QPoint(3, 3))); QApplication::sendEvent(D, &E); }, Qt::QueuedConnection);
        return;
    }
    CHECK(Displays.at(0)->text().isEmpty());
    Dialog->findChild<QPushButton *>("VxOk")->click();
}

int main(int argc, char ** argv)
{
    if (argc < 2 || (strcmp(argv[1], "write") != 0 && strcmp(argv[1], "read") != 0))
    {
        printf("usage: %s write|read [B]\n", argv[0]);
        return 2;
    }
    g_Write = strcmp(argv[1], "write") == 0;
    g_SetB = argc > 2 && strcmp(argv[2], "B") == 0;
    g_Cases = g_SetB ? CasesB : Cases;
    QApplication App(argc, argv);
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
    QTimer::singleShot(200, Drive);
    g_Plugins->ConfigPlugin(nullptr, PLUGIN_TYPE_CONTROLLER);
    CHECK(g_Seen);
    AppCleanup();
    printf(g_Failures == 0 ? "PASS input-capture %s%s\n" : "FAILED input-capture %s%s\n", argv[1], g_SetB ? " B" : "");
    return g_Failures == 0 ? 0 : 1;
}
