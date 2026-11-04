// The Input dialog as a user sees it (Project64-input/ConfigQt.cpp): the Control 1-4 selector, the tabs Assignment / Settings /
// Test, the two columns of compact cards (every binding in view), what the bindings show in each language and while they wait,
// the sequential assignment and its cancel button, the context menu, the settings (pak), the big controller of the test tab
// (pressed inputs, stick), the layout from the smallest to a big size. VX_SHOTS=<dir> saves screenshots.
// Run with isolated XDG directories (Scripts/linux/run-offscreen-tests.sh).
//   input-ui-test <es|en>
#include <Common/path.h>
#include <Project64-cli/LinuxNotification.h>
#include <Project64-core/AppInit.h>
#include <Project64-core/Notification.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Plugins/ControllerPlugin.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/Settings.h>

#include "../Vortex/Theme.h"
#include "input_ui_access.h"
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <cstdio>
#include <cstring>
#include <tuple>

class DummyRenderWindow : public RenderWindow
{
public:
    void * GetWindowHandle(void) const override { return nullptr; }
};

static int g_Failures = 0, g_Checks = 0;
#define CHECK(cond, ...)                                                       \
    do                                                                         \
    {                                                                          \
        g_Checks++;                                                            \
        if (!(cond))                                                           \
        {                                                                      \
            printf("FAIL %s:%d: %s ", __FILE__, __LINE__, #cond);              \
            printf("" __VA_ARGS__);                                            \
            printf("\n");                                                      \
            g_Failures++;                                                      \
        }                                                                      \
    } while (0)

static bool g_Spanish = true;
static bool g_Seen = false;

static void Pump(int Ms)
{
    QEventLoop Loop;
    QTimer::singleShot(Ms, &Loop, &QEventLoop::quit);
    Loop.exec();
}

// Row numbers: the order of Setup (D-pad U D L R, stick U D L R, C U D L R, B, A, Start, Z, L, R)
enum { StickUp = 4, CUp = 8, B = 12, A = 13, Start = 14, Z = 15, L = 16, R = 17 };

static void SendKey(QWidget * Target, int Key, unsigned Native, Qt::KeyboardModifiers Mods = Qt::NoModifier)
{
    QKeyEvent Press(QEvent::KeyPress, Key, Mods, Native, 0, 0, QString());
    QApplication::sendEvent(Target, &Press);
    QKeyEvent Release(QEvent::KeyRelease, Key, Mods, Native, 0, 0, QString());
    QApplication::sendEvent(Target, &Release);
}

static void SendMouse(QWidget * Target, Qt::MouseButton Button)
{
    const QPointF P(2, 2);
    QMouseEvent Press(QEvent::MouseButtonPress, P, Target->mapToGlobal(P.toPoint()), Button, Button, Qt::NoModifier);
    QApplication::sendEvent(Target, &Press);
    QMouseEvent Release(QEvent::MouseButtonRelease, P, Target->mapToGlobal(P.toPoint()), Button, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(Target, &Release);
}

static void SendWheel(QWidget * Target, int AngleX, int AngleY)
{
    const QPointF P(2, 2);
    QWheelEvent Wheel(P, Target->mapToGlobal(P.toPoint()), QPoint(), QPoint(AngleX, AngleY), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(Target, &Wheel);
}

static const char * Tr(const char * Es, const char * En)
{
    return g_Spanish ? Es : En;
}

// A screenshot of the dialog when VX_SHOTS=<dir> is set: <dir>/input-<name>-<language>-<scale>.png (the scale of QT_SCALE_FACTOR)
static void Shot(QWidget * Dialog, const char * Name)
{
    const QByteArray Dir = qgetenv("VX_SHOTS");
    if (Dir.isEmpty())
    {
        return;
    }
    const QByteArray Scale = qEnvironmentVariableIsSet("QT_SCALE_FACTOR") ? qgetenv("QT_SCALE_FACTOR") : QByteArray("1");
    Dialog->grab().save(QString("%1/input-%2-%3-%4.png").arg(QString::fromUtf8(Dir)).arg(Name).arg(g_Spanish ? "es" : "en").arg(QString::fromUtf8(Scale)));
}

// every binding of the page is inside the part of the scroll area that is seen (nothing has to be scrolled to)
static bool AllInView(QScrollArea * Scroll, const QList<BindingRef> & Caps)
{
    const QRect View = Scroll->viewport()->rect();
    for (const BindingRef & Cap : Caps)
    {
        if (!View.contains(QRect(Cap.W->mapTo(Scroll->viewport(), QPoint(0, 0)), Cap.W->size())))
        {
            return false;
        }
    }
    return true;
}

static QList<QFrame *> CardsOf(QWidget * Host)
{
    QList<QFrame *> Result;
    for (QFrame * F : Host->findChildren<QFrame *>(QString(), Qt::FindDirectChildrenOnly))
    {
        if (F->property("vx").toString() == "card")
        {
            Result.push_back(F);
        }
    }
    return Result;
}

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
    Dialog->activateWindow();
    Dialog->resize(1040, 700);
    Pump(300);

    // ---- the selector of the control
    QPushButton * Control[4];
    for (int i = 0; i < 4; i++)
    {
        Control[i] = Dialog->findChild<QPushButton *>(QString("VxControl%1").arg(i + 1));
        CHECK(Control[i] != nullptr && Control[i]->isVisible(), "control %d selector", i + 1);
    }
    if (!Control[0] || !Control[1] || !Control[2] || !Control[3])
    {
        Dialog->reject();
        return;
    }
    CHECK(Control[0]->isChecked() && !Control[1]->isChecked());
    CHECK(Control[0]->text() == QString("Control 1"));
    QStackedWidget * Stack = nullptr;
    for (QStackedWidget * S : Dialog->findChildren<QStackedWidget *>())
    {
        if (Stack == nullptr && S->count() == 4)
        {
            Stack = S;
        }
    }
    CHECK(Stack != nullptr);
    if (Stack == nullptr)
    {
        Dialog->reject();
        return;
    }
    QWidget * Page = Stack->widget(0);
    QTabWidget * Tabs = Page->findChild<QTabWidget *>("VxInputTabs");
    CHECK(Tabs != nullptr && Tabs->count() == 3 && Tabs->tabText(0) == Tr("Asignación", "Assignment") && Tabs->tabText(1) == Tr("Ajustes", "Settings") && Tabs->tabText(2) == Tr("Prueba", "Test"));
    CHECK(Tabs != nullptr && Tabs->currentIndex() == 0, "the assignment is the first thing seen");
    // the header: the device and its state, one line
    QComboBox * Device = Page->findChild<QComboBox *>("VxInputDevice");
    QLabel * Status = Page->findChild<QLabel *>("VxInputStatus");
    CHECK(Device != nullptr && Device->isVisible() && Status != nullptr && Status->isVisible());
    CHECK(Status != nullptr && Status->text() == Tr("Conectado", "Connected"), "'%s'", Status ? Status->text().toUtf8().constData() : "");
    CHECK(Device != nullptr && Tabs != nullptr && Device->mapTo(Dialog, QPoint(0, 0)).y() < Tabs->mapTo(Dialog, QPoint(0, 0)).y(), "the device is above the tabs");
    CHECK(Tabs != nullptr && Tabs->mapTo(Dialog, QPoint(0, 0)).y() < 140, "the header is compact: tabs at y=%d", Tabs ? Tabs->mapTo(Dialog, QPoint(0, 0)).y() : -1);

    // ---- the assignment: five cards in two columns, every binding in view, no scroll at all in a normal window
    QWidget * Grid = Page->findChild<QWidget *>("VxInputCards");
    QScrollArea * Scroll = Page->findChild<QScrollArea *>("VxInputScroll");
    CHECK(Grid != nullptr && Scroll != nullptr && Scroll->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff);
    if (Grid == nullptr || Scroll == nullptr)
    {
        Dialog->reject();
        return;
    }
    QList<QFrame *> Cards = CardsOf(Grid);
    CHECK(Cards.size() == 5, "five cards (%d)", (int)Cards.size());
    QList<BindingRef> Caps = BindingDisplays(Page);
    CHECK(Caps.size() == 18);
    if (Caps.size() != 18 || Cards.size() != 5)
    {
        Dialog->reject();
        return;
    }
    auto Pos = [&](QWidget * W) { return W->mapTo(Dialog, QPoint(0, 0)); };
    CHECK(Pos(Cards[0]).x() < Pos(Cards[1]).x() && Pos(Cards[0]).y() == Pos(Cards[1]).y(), "movement and buttons side by side");
    CHECK(Pos(Cards[2]).x() == Pos(Cards[0]).x() && Pos(Cards[2]).y() > Pos(Cards[0]).y() && Pos(Cards[3]).x() == Pos(Cards[1]).x() && Pos(Cards[3]).y() == Pos(Cards[2]).y(), "C-buttons and D-pad in the second row");
    CHECK(Pos(Cards[4]).x() == Pos(Cards[0]).x() && Pos(Cards[4]).y() > Pos(Cards[2]).y(), "the shoulders in the third");
    CHECK(Cards[0]->width() < Dialog->width() / 2 + 10 && Cards[0]->height() < 190, "compact cards (%dx%d)", Cards[0]->width(), Cards[0]->height());
    CHECK(AllInView(Scroll, Caps) && Scroll->verticalScrollBar()->maximum() == 0, "every binding in view without scrolling (scroll %d)", Scroll->verticalScrollBar()->maximum());
    CHECK(Scroll->horizontalScrollBar()->maximum() == 0);
    CHECK(Caps.at(A).W->width() <= 230 && Caps.at(A).W->width() == Caps.at(Z).W->width(), "bindings of a reasonable, equal width (%d)", Caps.at(A).W->width());
    for (const char * Id : {"VxInputReset", "VxOk", "VxCancel", "VxApply", "VxInputSetup", "VxInputDefaults"})
    {
        QPushButton * B_ = Page->findChild<QPushButton *>(Id); if (B_ == nullptr) { B_ = Dialog->findChild<QPushButton *>(Id); }
        CHECK(B_ != nullptr && B_->isVisible() && Dialog->rect().contains(B_->mapTo(Dialog, B_->rect().bottomRight())), "%s visible in the dialog", Id);
    }
    QPushButton * CancelAssign = Page->findChild<QPushButton *>("VxInputCancelAssign");
    CHECK(CancelAssign != nullptr && !CancelAssign->isVisible(), "no cancel button while nothing is assigned");
    // no big drawing of the controller in the assignment
    QWidget * Pad = Page->findChild<QWidget *>("VxController");
    CHECK(Pad != nullptr && !Pad->isVisible(), "the drawing is on the test tab");
    Shot(Dialog, "assignment");

    // ---- every size: nothing cut, no horizontal scroll; two columns while they fit, one when they do not
    for (const QSize Size : {QSize(640, 480), QSize(820, 560), QSize(1040, 700), QSize(1500, 950)})
    {
        Dialog->resize(Size);
        Pump(200);
        CHECK(Scroll->horizontalScrollBar()->maximum() == 0, "no horizontal scroll at %dx%d", Size.width(), Size.height());
        CHECK(Grid->width() <= Scroll->viewport()->width() + 1, "the cards fit the width at %dx%d", Size.width(), Size.height());
        const bool Two = Pos(Cards[1]).y() == Pos(Cards[0]).y();
        CHECK(Two, "two columns at %dx%d", Size.width(), Size.height());
        CHECK(Dialog->rect().contains(Dialog->findChild<QPushButton *>("VxOk")->mapTo(Dialog, QPoint(0, 0))) && Dialog->findChild<QPushButton *>("VxOk")->isVisible(), "footer inside at %dx%d", Size.width(), Size.height());
        if (Size.height() >= 700)
        {
            CHECK(AllInView(Scroll, Caps), "every binding in view at %dx%d", Size.width(), Size.height());
        }
        for (const BindingRef & Cap : Caps)
        {
            CHECK(Cap.W->width() > 60 && Cap.W->height() > 14, "binding is not cut");
        }
        // the cards are never wider than their column, whatever the number of columns
        for (QFrame * Card : Cards)
        {
            CHECK(Card->width() <= Scroll->viewport()->width(), "card inside at %dx%d", Size.width(), Size.height());
        }
    }
    // a width that does not hold two cards (a big font, a tiny window): one column, scrolling vertically only
    Dialog->setMinimumSize(380, 400);
    Dialog->resize(400, 560);
    Pump(250);
    CHECK(Pos(Cards[1]).y() > Pos(Cards[0]).y() && Pos(Cards[1]).x() == Pos(Cards[0]).x(), "one column at 400 wide");
    CHECK(Scroll->horizontalScrollBar()->maximum() == 0 && Scroll->verticalScrollBar()->maximum() > 0, "vertical scroll only (%d, %d)", Scroll->horizontalScrollBar()->maximum(), Scroll->verticalScrollBar()->maximum());
    CHECK(Grid->width() <= Scroll->viewport()->width() + 1 && Cards[0]->width() <= Scroll->viewport()->width());
    Scroll->verticalScrollBar()->setValue(Scroll->verticalScrollBar()->maximum());
    Pump(100);
    CHECK(Pos(Dialog->findChild<QPushButton *>("VxOk")).y() + 10 < Dialog->height(), "footer still visible when scrolled");
    Scroll->verticalScrollBar()->setValue(0);
    Dialog->setMinimumSize(640, 480);
    Dialog->resize(1040, 700);
    Pump(200);

    // ---- the bindings saved (control 1 has the defaults: the keyboard)
    CHECK(Caps.at(A)->text() == "X" && Caps.at(B)->text() == "C" && Caps.at(Z)->text() == "Z", "the defaults are shown");
    CHECK(Caps.at(Start).W->text() == Tr("Intro", "Enter"), "'%s'", Caps.at(Start).W->text().toUtf8().constData());
    CHECK(Caps.at(StickUp).W->text() == Tr("Teclado num. 8", "Numpad 8"), "'%s'", Caps.at(StickUp).W->text().toUtf8().constData());

    // ---- the clear button shows while the pointer is on the row (the context menu clears too)
    QWidget * HostA = Caps.at(A).W->parentWidget();
    QToolButton * ClearA = HostA->findChild<QToolButton *>("VxClear");
    CHECK(ClearA != nullptr && !ClearA->isVisible(), "no clear button on a quiet row");
    QEnterEvent Enter(QPointF(3, 3), QPointF(3, 3), QPointF(3, 3));
    QApplication::sendEvent(HostA, &Enter);
    CHECK(HostA->property("hot").toBool() && ClearA->isVisible() && ClearA->text() == QString::fromUtf8("×"), "the clear button of the row under the pointer");
    QEvent Leave(QEvent::Leave);
    QApplication::sendEvent(HostA, &Leave);
    CHECK(!HostA->property("hot").toBool() && !ClearA->isVisible(), "and not after leaving");

    // ---- capturing: the capsule says it waits, the row stands out, the cancel button appears
    Caps.at(A).W->click();
    CHECK(Caps.at(A).W->property("capturing").toBool(), "capturing");
    CHECK(Caps.at(A).W->text() == Tr("Pulsa una entrada…", "Press input…"), "'%s'", Caps.at(A).W->text().toUtf8().constData());
    CHECK(HostA->property("current").toBool(), "the row that waits stands out");
    CHECK(CancelAssign->isVisible() && CancelAssign->text() == Tr("Cancelar asignación", "Cancel assignment"));
    CHECK(!Caps.at(A).W->accessibleDescription().isEmpty() && Caps.at(A).W->accessibleDescription() != Caps.at(A).W->property("binding").toString());
    Pump(100);
    Shot(Dialog, "capturing-a");
    // the left click of the cancel button is the button's, not an input
    SendMouse(CancelAssign, Qt::LeftButton);
    Pump(120);
    CHECK(!Caps.at(A).W->property("capturing").toBool() && Caps.at(A)->text() == "X" && !CancelAssign->isVisible() && !HostA->property("current").toBool(), "cancelled: A keeps X ('%s')", Caps.at(A)->text().toUtf8().constData());
    Caps.at(A).W->click();
    // another row asks for an input: the first one gives up and shows its binding again
    Caps.at(B).W->click();
    CHECK(!Caps.at(A).W->property("capturing").toBool() && Caps.at(A)->text() == "X", "the first row is restored");
    CHECK(Caps.at(B).W->property("capturing").toBool());
    // the mouse (the right button too: it is an input here, not a menu), shown with its name
    SendMouse(Caps.at(B).W, Qt::RightButton);
    Pump(150);
    CHECK(!Caps.at(B).W->property("capturing").toBool(), "the right button ended the wait");
    CHECK(QApplication::activePopupWidget() == nullptr, "no menu while waiting");
    CHECK(Caps.at(B)->text() == "Mouse Right" && Caps.at(B).W->text() == Tr("Mouse derecho", "Right Mouse"), "'%s'", Caps.at(B).W->text().toUtf8().constData());
    struct MouseCase { Qt::MouseButton Button; const char * Raw; const char * Es; const char * En; };
    const MouseCase Mouse[] = {{Qt::LeftButton, "Mouse Left", "Mouse izquierdo", "Left Mouse"}, {Qt::MiddleButton, "Mouse Middle", "Mouse central", "Middle Mouse"},
        {Qt::BackButton, "Mouse 4 (Back)", "Mouse atrás", "Mouse Back"}, {Qt::ForwardButton, "Mouse 5 (Forward)", "Mouse adelante", "Mouse Forward"}, {Qt::ExtraButton3, "Mouse 6", "Mouse 6", "Mouse 6"}};
    for (const MouseCase & M : Mouse)
    {
        Caps.at(A).W->click();
        SendMouse(Caps.at(A).W, M.Button);
        Pump(120);
        CHECK(Caps.at(A)->text() == M.Raw && Caps.at(A).W->text() == Tr(M.Es, M.En), "%s -> '%s'", M.Raw, Caps.at(A).W->text().toUtf8().constData());
    }
    struct WheelCase { int X, Y; const char * Raw; const char * Es; const char * En; };
    const WheelCase Wheels[] = {{0, 120, "Mouse Wheel Up", "Rueda ↑", "Wheel Up"}, {0, -120, "Mouse Wheel Down", "Rueda ↓", "Wheel Down"},
        {120, 0, "Mouse Wheel Left", "Rueda ←", "Wheel Left"}, {-120, 0, "Mouse Wheel Right", "Rueda →", "Wheel Right"}};
    for (const WheelCase & W : Wheels)
    {
        Caps.at(Z).W->click();
        SendWheel(Caps.at(Z).W, W.X, W.Y);
        Pump(120);
        CHECK(Caps.at(Z)->text() == W.Raw && Caps.at(Z).W->text() == QString::fromUtf8(Tr(W.Es, W.En)), "%s -> '%s'", W.Raw, Caps.at(Z).W->text().toUtf8().constData());
    }
    // keys with a name: Escape, Tab and Enter are assignable while waiting; Shift/Ctrl/Alt/Super in both sides
    struct KeyCase { int Key; unsigned Native; const char * Raw; const char * Es; const char * En; };
    const KeyCase Keys[] = {{Qt::Key_Escape, 9, "Escape", "Esc", "Esc"}, {Qt::Key_Tab, 23, "Tab", "Tab", "Tab"}, {Qt::Key_Return, 36, "Return", "Intro", "Enter"},
        {Qt::Key_Shift, 50, "Left Shift", "Shift izq.", "Left Shift"}, {Qt::Key_Shift, 62, "Right Shift", "Shift der.", "Right Shift"},
        {Qt::Key_Control, 37, "Left Ctrl", "Ctrl izq.", "Left Ctrl"}, {Qt::Key_Control, 105, "Right Ctrl", "Ctrl der.", "Right Ctrl"},
        {Qt::Key_Alt, 64, "Left Alt", "Alt izq.", "Left Alt"}, {Qt::Key_Alt, 108, "Right Alt", "Alt der.", "Right Alt"},
        {Qt::Key_Super_L, 133, "Left GUI", "Súper izq.", "Left Super"}, {Qt::Key_Super_R, 134, "Right GUI", "Súper der.", "Right Super"},
        {Qt::Key_1, 87, "Keypad 1", "Teclado num. 1", "Numpad 1"}, {Qt::Key_Slash, 61, "/", "/", "/"}, {Qt::Key_Comma, 59, ",", ",", ","}};
    for (const KeyCase & K : Keys)
    {
        Caps.at(Start).W->click();
        SendKey(Caps.at(Start).W, K.Key, K.Native);
        Pump(100);
        CHECK(!Caps.at(Start).W->property("capturing").toBool(), "%s ended the wait", K.Raw);
        CHECK(Caps.at(Start)->text() == K.Raw && Caps.at(Start).W->text() == QString::fromUtf8(Tr(K.Es, K.En)), "%s -> '%s' (%s)", K.Raw, Caps.at(Start).W->text().toUtf8().constData(), Caps.at(Start)->text().toUtf8().constData());
    }

    // ---- outside the wait: the context menu assigns or clears
    QTimer::singleShot(250, []() {
        QMenu * Menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        CHECK(Menu != nullptr, "the menu is shown when nothing waits");
        if (Menu != nullptr)
        {
            QStringList Names;
            for (QAction * Act : Menu->actions())
            {
                Names << Act->text();
            }
            CHECK(Names.size() == 2 && Names.at(0) == Tr("Asignar…", "Assign…") && Names.at(1) == Tr("Borrar", "Clear"), "%s", Names.join("|").toUtf8().constData());
            Menu->close();
        }
    });
    QContextMenuEvent Ctx(QContextMenuEvent::Mouse, QPoint(3, 3), Caps.at(A).W->mapToGlobal(QPoint(3, 3)));
    QApplication::sendEvent(Caps.at(A).W, &Ctx);
    Pump(500);
    CHECK(QApplication::activePopupWidget() == nullptr);
    // (Assign… of the menu is the click of the row; Clear is the clear button's job too)
    QApplication::sendEvent(HostA, &Enter);
    ClearA->click();
    QApplication::sendEvent(HostA, &Leave);
    CHECK(Caps.at(A)->text().isEmpty() && Caps.at(A).W->text() == QString::fromUtf8("—") && !ClearA->isVisible(), "cleared: '%s'", Caps.at(A).W->text().toUtf8().constData());

    // ---- the sequence: the cards' order, the row that waits stands out, the progress shows, the cancel button ends it
    QPushButton * Setup = Page->findChild<QPushButton *>("VxInputSetup");
    QPushButton * Defaults = Page->findChild<QPushButton *>("VxInputDefaults");
    QLabel * Progress = Page->findChild<QLabel *>("VxInputSequence");
    CHECK(Setup != nullptr && Setup->text() == Tr("Asignar en secuencia", "Assign sequentially") && Defaults != nullptr && Defaults->text() == Tr("Valores por defecto", "Defaults") && Progress != nullptr);
    Setup->click();
    Pump(100);
    const int Order[] = {StickUp, StickUp + 1, StickUp + 2, StickUp + 3, A, B, Z, Start, CUp, CUp + 1, CUp + 2, CUp + 3, 0, 1, 2, 3, L, R};
    CHECK(Caps.at(Order[0]).W->property("capturing").toBool() && CancelAssign->isVisible() && Progress->isVisible(), "the sequence starts with the first row");
    CHECK(Progress->text() == Tr("Asignando 1 de 18: ", "Assigning 1 of 18: ") + QString("Stick ↑"), "'%s'", Progress->text().toUtf8().constData());
    Shot(Dialog, "sequence");
    // each input goes to the row that waits and the next one waits at once
    const unsigned Natives[18] = {24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 38, 39, 40, 41, 52, 53};
    const int Qt_[18] = {Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T, Qt::Key_Y, Qt::Key_U, Qt::Key_I, Qt::Key_O, Qt::Key_P, Qt::Key_BracketLeft, Qt::Key_BracketRight,
        Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F, Qt::Key_Z, Qt::Key_X};
    bool Cancelled = false;
    for (int i = 0; i < 18; i++)
    {
        CHECK(Caps.at(Order[i]).W->property("capturing").toBool(), "step %d waits on its row", i);
        for (int j = 0; j < 18; j++)
        {
            CHECK(j == Order[i] || !Caps.at(j).W->property("capturing").toBool(), "only one row waits (%d)", j);
        }
        if (i == 3)
        {
            // Escape is an input like any other, it does not end the sequence
            SendKey(Caps.at(Order[i]).W, Qt::Key_Escape, 9);
            Pump(80);
            CHECK(Caps.at(Order[i])->text() == "Escape" && Caps.at(Order[i + 1]).W->property("capturing").toBool(), "Escape was assigned and the sequence went on");
            i++;
            CHECK(Caps.at(Order[i]).W->property("capturing").toBool());
        }
        if (i == 6 && !Cancelled)
        {
            // the cancel button stops the sequence half way: what was assigned stays, nothing waits
            SendMouse(CancelAssign, Qt::LeftButton);
            Pump(100);
            for (int j = 0; j < 18; j++)
            {
                CHECK(!Caps.at(j).W->property("capturing").toBool(), "cancelled: nothing waits");
            }
            CHECK(!CancelAssign->isVisible() && !Progress->isVisible() && Caps.at(Order[0])->text() == "Q", "cancelled half way");
            Cancelled = true;
            // and the sequence can be started again, to the end
            Setup->click();
            Pump(80);
            i = -1;
            continue;
        }
        SendKey(Caps.at(Order[i]).W, Qt_[i], Natives[i]);
        Pump(80);
    }
    for (int j = 0; j < 18; j++)
    {
        CHECK(!Caps.at(j).W->property("capturing").toBool(), "the sequence is over");
    }
    CHECK(!CancelAssign->isVisible() && !Progress->isVisible() && Setup->isEnabled(), "the buttons of the sequence are gone");
    CHECK(Caps.at(R)->text() == "X" && Caps.at(L)->text() == "Z", "the last rows got the last inputs ('%s', '%s')", Caps.at(L)->text().toUtf8().constData(), Caps.at(R)->text().toUtf8().constData());
    // changing the tab ends a wait
    Setup->click();
    Pump(50);
    Tabs->setCurrentIndex(1);
    Pump(100);
    CHECK(!Caps.at(Order[0]).W->property("capturing").toBool() && !CancelAssign->isVisible(), "leaving the tab ends the sequence");
    Tabs->setCurrentIndex(0);
    Pump(50);
    Defaults->click();
    Pump(100);
    CHECK(Caps.at(A)->text() == "X" && Caps.at(B)->text() == "C" && Caps.at(Z)->text() == "Z", "the defaults come back ('%s')", Caps.at(A)->text().toUtf8().constData());

    // ---- the settings: pak, stick, mouse and general options, no horizontal scroll
    Tabs->setCurrentIndex(1);
    Pump(200);
    QScrollArea * SettingsScroll = Page->findChild<QScrollArea *>("VxSettingsScroll");
    QWidget * SettingsGrid = Page->findChild<QWidget *>("VxSettingsCards");
    CHECK(SettingsScroll != nullptr && SettingsGrid != nullptr && CardsOf(SettingsGrid).size() == 4, "settings: four cards");
    QPushButton * PakRumble = Page->findChild<QPushButton *>("VxPakRumble");
    QPushButton * PakNone = Page->findChild<QPushButton *>("VxPakNone");
    QPushButton * PakMemory = Page->findChild<QPushButton *>("VxPakMemory");
    CHECK(PakRumble && PakNone && PakMemory && PakMemory->isChecked() && PakMemory->isVisible(), "control 1 has the memory pak");
    for (const char * Id : {"VxInputRange", "VxInputDeadZone", "VxInputSensitivity", "VxInputRealRange", "VxInputRemoveDuplicate", "VxInputShortcuts"})
    {
        QWidget * W_ = Page->findChild<QWidget *>(Id);
        CHECK(W_ != nullptr && W_->isVisible(), "%s in the settings", Id);
    }
    for (const QSize Size : {QSize(640, 480), QSize(1040, 700)})
    {
        Dialog->resize(Size);
        Pump(200);
        CHECK(SettingsScroll != nullptr && SettingsScroll->horizontalScrollBar()->maximum() == 0, "no horizontal scroll in the settings at %dx%d", Size.width(), Size.height());
    }
    Dialog->resize(1040, 700);
    Pump(200);
    Shot(Dialog, "settings");

    // ---- the test: the big controller; the pressed inputs light it up and the stick shows where it goes
    Tabs->setCurrentIndex(2);
    Pump(200);
    Pad = Page->findChild<QWidget *>("VxController");
    CHECK(Pad != nullptr && Pad->isVisible() && Pad->width() > 450 && Pad->height() > 300, "the big controller (%dx%d)", Pad ? Pad->width() : 0, Pad ? Pad->height() : 0);
    QLabel * Info = Page->findChild<QLabel *>("VxTestInfo");
    QLabel * StickText = Page->findChild<QLabel *>("VxTestStick");
    CHECK(Info != nullptr && Info->text() == Tr("Pulsa botones para comprobar la configuración", "Press buttons to check the configuration"), "'%s'", Info ? Info->text().toUtf8().constData() : "");
    CHECK(StickText != nullptr && StickText->text().contains("X: 0.00") && StickText->text().contains("Y: 0.00"), "'%s'", StickText ? StickText->text().toUtf8().constData() : "");
    CHECK(Pad->property("vxPressed").toUInt() == 0u, "nothing is pressed yet");
    Shot(Dialog, "test-idle");
    // A, B and C up (X, C, I of the defaults)
    QKeyEvent DownX(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier, 53, 0, 0, QString());
    QApplication::sendEvent(Dialog, &DownX);
    QKeyEvent DownC(QEvent::KeyPress, Qt::Key_C, Qt::NoModifier, 54, 0, 0, QString());
    QApplication::sendEvent(Dialog, &DownC);
    QKeyEvent DownI(QEvent::KeyPress, Qt::Key_I, Qt::NoModifier, 31, 0, 0, QString());
    QApplication::sendEvent(Dialog, &DownI);
    Pump(150);
    const unsigned Mask = Pad->property("vxPressed").toUInt();
    CHECK((Mask & (1u << A)) && (Mask & (1u << B)) && (Mask & (1u << CUp)) && !(Mask & (1u << Z)), "A, B and C up are lit (%u)", Mask);
    CHECK(Info->text().contains("A") && Info->text().contains("B") && Info->text().contains("C ↑"), "the list names them: '%s'", Info->text().toUtf8().constData());
    Shot(Dialog, "test-abc");
    QKeyEvent Up8(QEvent::KeyPress, Qt::Key_8, Qt::KeypadModifier, 80, 0, 0, QString());
    QApplication::sendEvent(Dialog, &Up8);
    QKeyEvent Left4(QEvent::KeyPress, Qt::Key_4, Qt::KeypadModifier, 83, 0, 0, QString());
    QApplication::sendEvent(Dialog, &Left4);
    Pump(150);
    CHECK((Pad->property("vxPressed").toUInt() & (1u << StickUp)) != 0, "stick up is lit");
    CHECK(Pad->property("vxStick").toPointF().y() > 0.6 && Pad->property("vxStick").toPointF().x() < -0.6, "the stick goes up and left (%f,%f)", Pad->property("vxStick").toPointF().x(), Pad->property("vxStick").toPointF().y());
    CHECK(StickText->text() != QString("") && !StickText->text().contains("X: 0.00"), "'%s'", StickText->text().toUtf8().constData());
    Shot(Dialog, "test-stick");
    for (auto [Key, Native, Mods] : {std::tuple<int, unsigned, Qt::KeyboardModifiers>{Qt::Key_X, 53, Qt::NoModifier}, {Qt::Key_C, 54, Qt::NoModifier}, {Qt::Key_I, 31, Qt::NoModifier},
             {Qt::Key_8, 80, Qt::KeypadModifier}, {Qt::Key_4, 83, Qt::KeypadModifier}})
    {
        QKeyEvent Rel(QEvent::KeyRelease, Key, Mods, Native, 0, 0, QString());
        QApplication::sendEvent(Dialog, &Rel);
    }
    Pump(150);
    CHECK(Pad->property("vxPressed").toUInt() == 0u && Pad->property("vxStick").toPointF().isNull(), "released: nothing lit");
    Tabs->setCurrentIndex(0);
    Pump(100);

    // ---- the four controls: each one has its own device, bindings and pak
    for (int i = 1; i < 4; i++)
    {
        Control[i]->click();
        Pump(150);
        CHECK(Control[i]->isChecked() && !Control[0]->isChecked() && Stack->currentIndex() == i, "control %d shown", i + 1);
        QWidget * P = Stack->widget(i);
        QList<BindingRef> C = BindingDisplays(P);
        QComboBox * Dev = P->findChild<QComboBox *>("VxInputDevice");
        CHECK(C.size() == 18 && Dev != nullptr);
        if (C.size() == 18 && Dev != nullptr)
        {
            const bool Plugged = Dev->currentData().toInt() == 1;
            CHECK(C.at(A).W->isEnabled() == Plugged, "control %d: the capsules follow the device", i + 1);
            QLabel * S = P->findChild<QLabel *>("VxInputStatus");
            CHECK(S != nullptr && S->text() == (Dev->currentData().toInt() == 0 ? QString(Tr("No conectado", "Not connected")) : QString(Tr("Conectado", "Connected"))));
            CHECK(P->findChild<QTabWidget *>("VxInputTabs")->currentIndex() == 0);
        }
    }
    Control[1]->click();
    Pump(100);
    QWidget * Page1 = Stack->widget(1);
    Page1->findChild<QTabWidget *>("VxInputTabs")->setCurrentIndex(1);
    Pump(100);
    Page1->findChild<QPushButton *>("VxPakRumble")->click();
    Control[0]->click();
    Pump(100);
    CHECK(PakMemory->isChecked() && !PakRumble->isChecked(), "back on control 1 its pak is shown");
    Control[1]->click();
    Pump(100);
    CHECK(Page1->findChild<QPushButton *>("VxPakRumble")->isChecked(), "control 2 keeps the one chosen");
    // a wait that is open when the control changes ends
    Control[0]->click();
    Pump(100);
    Tabs->setCurrentIndex(0);
    Pump(100);
    Caps.at(L).W->click();
    CHECK(Caps.at(L).W->property("capturing").toBool());
    Control[1]->click();
    Pump(100);
    CHECK(!Caps.at(L).W->property("capturing").toBool(), "changing control ends the wait");
    Control[0]->click();
    Pump(100);
    Dialog->findChild<QPushButton *>("VxCancel")->click();
}

int main(int argc, char ** argv)
{
    g_Spanish = argc < 2 || strcmp(argv[1], "en") != 0;
    QApplication App(argc, argv);
    Vortex::ApplyTheme(App); // (the look of the application: what the cards and the bindings are painted with)
    App.setProperty("vortexLanguage", g_Spanish ? "es" : "en");
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
    CHECK(g_Seen, "the dialog was shown");
    AppCleanup();
    printf(g_Failures == 0 ? "PASS input-ui %s (%d checks)\n" : "FAILED input-ui %s (%d checks)\n", g_Spanish ? "es" : "en", g_Checks);
    return g_Failures == 0 ? 0 : 1;
}
