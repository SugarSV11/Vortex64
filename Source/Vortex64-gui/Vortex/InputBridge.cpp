#include "stdafx.h"
#include "InputBridge.h"

#include <Vortex64-core/N64System/ConfigPause.h>
#include <Vortex64-core/N64System/SystemGlobals.h>
#include <Vortex64-core/Plugins/ControllerPlugin.h>
#include <Vortex64-core/Plugins/Plugin.h>
#include <Vortex64-core/Settings.h>

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QCursor>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QTime>
#include <QWheelEvent>
#include <QWidget>
#include <cstdio>
#include <cstdlib>

namespace Vortex
{
InputBridge::InputBridge(QWidget * Window) :
    QObject(Window),
    m_Window(Window)
{
    qApp->installEventFilter(this);
}

InputBridge::~InputBridge()
{
    if (qApp != nullptr)
    {
        qApp->removeEventFilter(this);
    }
}

int InputBridge::ButtonNumber(int QtButton)
{
    switch (QtButton)
    {
    case Qt::LeftButton: return 0;
    case Qt::MiddleButton: return 1;
    case Qt::RightButton: return 2;
    default:
        break;
    }
    // Back, Forward, Task and ExtraButton4.. are consecutive in Qt (ExtraButton1 = Back): SDL numbers them 4, 5, 6, ...
    if (QtButton >= Qt::ExtraButton1 && QtButton <= Qt::ExtraButton24)
    {
        int Index = 0;
        for (int Value = Qt::ExtraButton1; Value < QtButton; Value <<= 1)
        {
            Index++;
        }
        return 3 + Index <= 31 ? 3 + Index : -1;
    }
    return -1;
}

bool InputBridge::Active() const
{
    if (g_Plugins == nullptr || g_Settings == nullptr || !g_Settings->LoadBool(GameRunning_CPU_Running) || QApplication::activeWindow() != m_Window || ConfigPause::OpenCount() > 0)
    {
        return false; // (a window of configuration is open: its keys and clicks are its own, and the game is paused)
    }
    // an open menu or a field that is typed in has the keys and the clicks: they are not the game's
    if (QApplication::activePopupWidget() != nullptr)
    {
        return false;
    }
    const QWidget * Focus = QApplication::focusWidget();
    return !(qobject_cast<const QLineEdit *>(Focus) != nullptr || qobject_cast<const QAbstractSpinBox *>(Focus) != nullptr || qobject_cast<const QTextEdit *>(Focus) != nullptr || qobject_cast<const QPlainTextEdit *>(Focus) != nullptr ||
             qobject_cast<const QComboBox *>(Focus) != nullptr);
}

// VORTEX_INPUT_TRACE=1: what the bridge sees and what it does with it (for a session where the keys do not reach the game)
static bool Trace()
{
    static const bool On = getenv("VORTEX_INPUT_TRACE") != nullptr;
    return On;
}

void InputBridge::Send(int Kind, int Code, int Value)
{
    if (g_Plugins == nullptr || g_Plugins->Control() == nullptr)
    {
        return;
    }
    // (looked up at every event, not kept: a plugin that is changed or reloaded takes its library with it)
    m_Function = reinterpret_cast<void (*)(int, int, int)>(g_Plugins->Control()->OptionalExport("VortexInputEvent"));
    if (Trace())
    {
        fprintf(stderr, "[input] %s bridge: kind %d code %d value %d -> %s\n", QTime::currentTime().toString("ss.zzz").toUtf8().constData(), Kind, Code, Value, m_Function != nullptr ? "plugin" : "no VortexInputEvent in the input plugin");
    }
    if (m_Function != nullptr)
    {
        m_Function(Kind, Code, Value);
    }
}

// Is a modifier held that is not a button of the pad? (a Ctrl that the game uses as a button is not the Ctrl of Ctrl+S)
bool InputBridge::HasRealModifier(Qt::KeyboardModifiers Held) const
{
    struct Modifier
    {
        Qt::KeyboardModifier Flag;
        int Codes[2]; // evdev: left and right
    };
    static const Modifier Modifiers[] = {{Qt::ControlModifier, {29, 97}}, {Qt::AltModifier, {56, 100}}, {Qt::MetaModifier, {125, 126}}, {Qt::ShiftModifier, {42, 54}}};
    for (const Modifier & M : Modifiers)
    {
        if (!(Held & M.Flag))
        {
            continue;
        }
        bool AllPad = false;
        for (int Code : M.Codes)
        {
            if (m_Keys.contains(Code))
            {
                if (!KeyIsAssigned(Code))
                {
                    return true;
                }
                AllPad = true;
            }
        }
        if (!AllPad)
        {
            return true; // (a modifier the bridge did not see go down: not a button of the pad)
        }
    }
    return false;
}

bool InputBridge::KeyIsAssigned(int Code) const
{
    if (g_Plugins == nullptr || g_Plugins->Control() == nullptr)
    {
        return false;
    }
    auto Function = reinterpret_cast<int (*)(int)>(g_Plugins->Control()->OptionalExport("VortexInputIsKeyAssigned"));
    return Function != nullptr && Function(Code) != 0;
}

void InputBridge::ReleaseAll()
{
    if (!m_Keys.isEmpty() || !m_Buttons.isEmpty())
    {
        Send(4, 0, 0);
    }
    m_Keys.clear();
    m_Buttons.clear();
    m_HaveLast = false;
}

bool InputBridge::eventFilter(QObject * Watched, QEvent * Event)
{
    if (Trace() && (Event->type() == QEvent::KeyPress || Event->type() == QEvent::MouseButtonPress))
    {
        fprintf(stderr, "[input] bridge: %s on %s, game running %d, active window is ours %d\n", Event->type() == QEvent::KeyPress ? "key press" : "button press", Watched->metaObject()->className(),
                g_Settings != nullptr ? (int)g_Settings->LoadBool(GameRunning_CPU_Running) : -1, (int)(QApplication::activeWindow() == m_Window));
    }
    switch (Event->type())
    {
    case QEvent::WindowDeactivate:
    case QEvent::ApplicationDeactivate:
        ReleaseAll();
        return false;
    case QEvent::ShortcutOverride: // (a key that is also a shortcut of the window, as Esc is of the full screen: Qt turns the
                                   // press into the shortcut and the press event never comes; the key is still a key of the pad)
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    {
        const QKeyEvent * Key = static_cast<QKeyEvent *>(Event);
        // (the code of the key of the X server is the evdev one plus 8; an event that is a repetition changes nothing)
        const int Code = (int)Key->nativeScanCode() - 8;
        if (Event->type() == QEvent::ShortcutOverride)
        {
            // One key, one action. A key that is a button of the pad is the game's: accepting the override makes Qt deliver the key as a
            // key press, and the shortcut that it may also be does not run. A key with a modifier that is not itself a button of the pad
            // (Ctrl+S, Alt+Enter) is a combination, never a button: the shortcut runs, and the pad does not see it. (Without a shortcut
            // for it Qt delivers the key press, which the game gets as any other.)
            if (Active() && !HasRealModifier(Key->modifiers()) && KeyIsAssigned(Code))
            {
                Event->accept();
                if (Trace())
                {
                    fprintf(stderr, "[input] bridge: key %d is a button of the pad: the shortcut does not run\n", Code);
                }
                return true; // (accepted: the key press follows and gives the key to the game)
            }
            return false;
        }
        if (Key->isAutoRepeat() || Code <= 0)
        {
            return false;
        }
        const bool Down = Event->type() != QEvent::KeyRelease;
        if (!Down && m_Keys.contains(Code))
        {
            m_Keys.remove(Code); // (a key that was pressed while the game ran is released whatever the window is now)
            Send(0, Code, 0);
        }
        else if (Down && !m_Keys.contains(Code) && Active())
        {
            m_Keys.insert(Code);
            Send(0, Code, 1);
        }
        return false;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    {
        const QMouseEvent * Mouse = static_cast<QMouseEvent *>(Event);
        const int Number = ButtonNumber((int)Mouse->button());
        if (Number < 0)
        {
            return false;
        }
        const bool Down = Event->type() == QEvent::MouseButtonPress;
        if (!Down && m_Buttons.contains(Number))
        {
            m_Buttons.remove(Number);
            Send(1, Number, 0);
        }
        else if (Down && Active())
        {
            m_Buttons.insert(Number);
            Send(1, Number, 1);
        }
        return false;
    }
    case QEvent::Wheel:
    {
        if (Active())
        {
            const QWheelEvent * Wheel = static_cast<QWheelEvent *>(Event);
            const QPoint Angle = Wheel->angleDelta();
            if (Angle != QPoint(0, 0))
            {
                // the same reading of the wheel as the configuration window
                const int Direction = qAbs(Angle.y()) >= qAbs(Angle.x()) ? (Angle.y() > 0 ? 0 : 1) : (Angle.x() > 0 ? 2 : 3);
                Send(2, Direction, 1);
            }
        }
        return false;
    }
    case QEvent::MouseMove:
    {
        if (Active() && Watched->isWidgetType())
        {
            const QPoint Now = static_cast<QMouseEvent *>(Event)->globalPosition().toPoint();
            if (m_HaveLast)
            {
                const QPoint Delta = Now - m_LastPointer;
                if (Delta != QPoint(0, 0))
                {
                    Send(3, Delta.x(), Delta.y());
                }
            }
            m_LastPointer = Now;
            m_HaveLast = true;
        }
        else
        {
            m_HaveLast = false;
        }
        return false;
    }
    default:
        return false;
    }
}
} // namespace Vortex
