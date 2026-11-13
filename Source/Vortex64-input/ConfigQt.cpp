// Qt6 counterpart of the Windows configuration property sheet in
// InputConfigUI.cpp / OptionsUI.cpp / ShortcutsUI.cpp / wtl-ScanButton.cpp: one
// "Player N" page per controller with the same rows, sliders and buttons, the
// same Options and Shortcuts dialogs and the same button scan (Setup walks the
// rows in the same order, the scan times out after 10 seconds, a duplicate
// mapping is removed when the option asks for it). Built as
// libVortex64-input-qtconfig.so and loaded on demand by DllConfig() so the
// plugin itself has no Qt dependency.
//
// Keyboard and mouse scanning: on Windows the plugin reads the devices itself;
// here SDL only sees them while one of its windows has the focus, which the Qt
// dialog does not give it. While a row waits for input an application wide event
// filter takes every key, mouse button and wheel movement before any widget (so
// Escape, Enter, Space, Tab, the arrows, the modifiers and the left button of the
// mouse can be assigned too) and turns it into the BUTTON the plugin reads at run
// time: the identity of a key is its physical scancode (the X keycode, the same
// that SDL reports, never the text of the key), that of a mouse button is its SDL
// button number, the wheel is a momentary button. Joystick scanning still goes
// through CProject64Input::ScanDevices().
#include "CProject64Input.h"
#include "N64Controller.h"
#include "Shortcuts.h"
#include "N64ControllerWidget.h"

#include <Vortex64-plugin-spec/Input.h>
#include <SDL2/SDL_scancode.h>
#include "LinuxScancodes.h"

#include <QApplication>
#include <QIcon>
#include <QButtonGroup>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QGuiApplication>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <set>
#include <cmath>
#include <QtMath>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QToolTip>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cstdio>
#include <QContextMenuEvent>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QTabWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QFrame>
#include <QVBoxLayout>

#include <cstring>
#include <functional>
#include <vector>

namespace
{
QString FromWide(const std::wstring & Text)
{
    return QString::fromStdWString(Text);
}

// Spanish with the Spanish language of Vortex64 (the application sets the property "vortexLanguage"), English otherwise
QString Tx(const char * Spanish, const char * English)
{
    return QString::fromUtf8(qApp->property("vortexLanguage").toString() == QLatin1String("es") ? Spanish : English);
}

// PJ64_INPUT_DEBUG=1: what Qt delivers while the dialog waits for an input, and what becomes of it (stderr). A tool for the
// reports of keys or buttons that cannot be assigned on a real desktop.
bool InputDebug()
{
    static const bool On = qEnvironmentVariableIntValue("PJ64_INPUT_DEBUG") > 0;
    return On;
}

bool SameButton(const BUTTON & A, const BUTTON & B)
{
    return A.Offset == B.Offset && A.AxisID == B.AxisID && A.BtnType == B.BtnType && memcmp(&A.DeviceGuid, &B.DeviceGuid, sizeof(A.DeviceGuid)) == 0;
}

bool IsEmptyButton(const BUTTON & Button)
{
    BUTTON Empty = {};
    return memcmp(&Button, &Empty, sizeof(Button)) == 0;
}

// Physical key -> SDL scancode. Qt gives the X keycode (evdev code + 8) in
// nativeScanCode(); SDL translates the same evdev code with the same table, so
// both sides agree for every key of the keyboard whatever the layout.
int SdlScancodeFromNative(quint32 NativeScanCode)
{
    if (NativeScanCode < 8)
    {
        return 0;
    }
    const quint32 Evdev = NativeScanCode - 8;
    if (Evdev < sizeof(linux_scancode_table) / sizeof(linux_scancode_table[0]))
    {
        return (int)linux_scancode_table[Evdev];
    }
    return 0;
}

// Fallback when there is no native code (offscreen platform, synthetic events): Qt key -> SDL scancode
int SdlScancodeFromQtKey(int Key, bool Keypad)
{
    if (Key >= Qt::Key_A && Key <= Qt::Key_Z)
    {
        return SDL_SCANCODE_A + (Key - Qt::Key_A);
    }
    if (Keypad && Key >= Qt::Key_0 && Key <= Qt::Key_9)
    {
        return Key == Qt::Key_0 ? SDL_SCANCODE_KP_0 : SDL_SCANCODE_KP_1 + (Key - Qt::Key_1);
    }
    if (Key >= Qt::Key_1 && Key <= Qt::Key_9)
    {
        return SDL_SCANCODE_1 + (Key - Qt::Key_1);
    }
    if (Key >= Qt::Key_F1 && Key <= Qt::Key_F12)
    {
        return SDL_SCANCODE_F1 + (Key - Qt::Key_F1);
    }
    if (Key >= Qt::Key_F13 && Key <= Qt::Key_F24)
    {
        return SDL_SCANCODE_F13 + (Key - Qt::Key_F13);
    }
    switch (Key)
    {
    case Qt::Key_0: return SDL_SCANCODE_0;
    case Qt::Key_Return: return SDL_SCANCODE_RETURN;
    case Qt::Key_Enter: return SDL_SCANCODE_KP_ENTER;
    case Qt::Key_Escape: return SDL_SCANCODE_ESCAPE;
    case Qt::Key_Backspace: return SDL_SCANCODE_BACKSPACE;
    case Qt::Key_Tab: return SDL_SCANCODE_TAB;
    case Qt::Key_Backtab: return SDL_SCANCODE_TAB;
    case Qt::Key_Space: return SDL_SCANCODE_SPACE;
    case Qt::Key_Minus: return Keypad ? SDL_SCANCODE_KP_MINUS : SDL_SCANCODE_MINUS;
    case Qt::Key_Equal: return SDL_SCANCODE_EQUALS;
    case Qt::Key_BracketLeft: return SDL_SCANCODE_LEFTBRACKET;
    case Qt::Key_BracketRight: return SDL_SCANCODE_RIGHTBRACKET;
    case Qt::Key_Backslash: return SDL_SCANCODE_BACKSLASH;
    case Qt::Key_Semicolon: return SDL_SCANCODE_SEMICOLON;
    case Qt::Key_Apostrophe: return SDL_SCANCODE_APOSTROPHE;
    case Qt::Key_QuoteLeft: return SDL_SCANCODE_GRAVE;
    case Qt::Key_Comma: return Keypad ? SDL_SCANCODE_KP_COMMA : SDL_SCANCODE_COMMA;
    case Qt::Key_Period: return Keypad ? SDL_SCANCODE_KP_PERIOD : SDL_SCANCODE_PERIOD;
    case Qt::Key_Slash: return Keypad ? SDL_SCANCODE_KP_DIVIDE : SDL_SCANCODE_SLASH;
    case Qt::Key_Asterisk: return SDL_SCANCODE_KP_MULTIPLY;
    case Qt::Key_Plus: return SDL_SCANCODE_KP_PLUS;
    case Qt::Key_Insert: return SDL_SCANCODE_INSERT;
    case Qt::Key_Delete: return SDL_SCANCODE_DELETE;
    case Qt::Key_Home: return SDL_SCANCODE_HOME;
    case Qt::Key_End: return SDL_SCANCODE_END;
    case Qt::Key_PageUp: return SDL_SCANCODE_PAGEUP;
    case Qt::Key_PageDown: return SDL_SCANCODE_PAGEDOWN;
    case Qt::Key_Right: return SDL_SCANCODE_RIGHT;
    case Qt::Key_Left: return SDL_SCANCODE_LEFT;
    case Qt::Key_Down: return SDL_SCANCODE_DOWN;
    case Qt::Key_Up: return SDL_SCANCODE_UP;
    case Qt::Key_Pause: return SDL_SCANCODE_PAUSE;
    case Qt::Key_Print: return SDL_SCANCODE_PRINTSCREEN;
    case Qt::Key_ScrollLock: return SDL_SCANCODE_SCROLLLOCK;
    case Qt::Key_CapsLock: return SDL_SCANCODE_CAPSLOCK;
    case Qt::Key_NumLock: return SDL_SCANCODE_NUMLOCKCLEAR;
    case Qt::Key_Menu: return SDL_SCANCODE_APPLICATION;
    case Qt::Key_Shift: return SDL_SCANCODE_LSHIFT;
    case Qt::Key_Control: return SDL_SCANCODE_LCTRL;
    case Qt::Key_Alt: return SDL_SCANCODE_LALT;
    case Qt::Key_AltGr: return SDL_SCANCODE_RALT;
    case Qt::Key_Meta: return SDL_SCANCODE_LGUI;
    case Qt::Key_Super_L: return SDL_SCANCODE_LGUI;
    case Qt::Key_Super_R: return SDL_SCANCODE_RGUI;
    case Qt::Key_Less: return SDL_SCANCODE_NONUSBACKSLASH;
    }
    return 0;
}

int SdlScancodeFromEvent(const QKeyEvent * Event)
{
    const int Native = SdlScancodeFromNative(Event->nativeScanCode());
    if (Native != 0)
    {
        return Native;
    }
    return SdlScancodeFromQtKey(Event->key(), (Event->modifiers() & Qt::KeypadModifier) != 0);
}

// Qt mouse button -> number of the button in the SDL state (bit of SDL_GetMouseState); -1: none
int SdlMouseButton(Qt::MouseButton Button)
{
    switch (Button)
    {
    case Qt::LeftButton: return 0;
    case Qt::MiddleButton: return 1;
    case Qt::RightButton: return 2;
    case Qt::BackButton: return 3;
    case Qt::ForwardButton: return 4;
    default: break;
    }
    // ExtraButton3 (0x20) .. ExtraButton24: SDL 6, 7, 8 ... (the state of SDL holds 32 bits)
    int Bit = 0;
    for (unsigned Value = (unsigned)Button; Value > 1; Value >>= 1)
    {
        Bit++;
    }
    if (Bit >= 5 && (unsigned)Button == (1u << Bit))
    {
        const int Index = Bit; // 0x20 -> bit 5 -> SDL button 6 (index 5)
        return Index < 32 ? Index : -1;
    }
    return -1;
}

void Repolish(QWidget * Widget)
{
    Widget->style()->unpolish(Widget);
    Widget->style()->polish(Widget);
    Widget->update();
}

// The name of an assignment as it is shown to the user (the plugin's ButtonAssignment keeps the English technical text that
// the tests and the log use). Keys are the SDL names; the mouse and wheel are named as users call them.
QString PrettyBinding(const BUTTON & Button, const QString & Raw)
{
    if (Raw.isEmpty())
    {
        return Raw;
    }
    if (Button.BtnType == BTNTYPE_MOUSEBUTTON)
    {
        static const char * const Es[] = {"Mouse izquierdo", "Mouse central", "Mouse derecho", "Mouse atrás", "Mouse adelante"};
        static const char * const En[] = {"Left Mouse", "Middle Mouse", "Right Mouse", "Mouse Back", "Mouse Forward"};
        static const char * const WheelEs[] = {"Rueda ↑", "Rueda ↓", "Rueda ←", "Rueda →"};
        static const char * const WheelEn[] = {"Wheel Up", "Wheel Down", "Wheel Left", "Wheel Right"};
        if (Button.Offset < 5)
        {
            return Tx(Es[Button.Offset], En[Button.Offset]);
        }
        if (Button.Offset >= 32 && Button.Offset < 36)
        {
            return Tx(WheelEs[Button.Offset - 32], WheelEn[Button.Offset - 32]);
        }
        return QString("Mouse %1").arg((unsigned)Button.Offset + 1);
    }
    if (Button.BtnType == BTNTYPE_JOYBUTTON)
    {
        return Tx("Botón ", "Button ") + QString::number((unsigned)Button.Offset);
    }
    if (Button.BtnType == BTNTYPE_KEYBUTTON)
    {
        static const struct
        {
            const char * Sdl;
            const char * Es;
            const char * En;
        } Names[] = {
            {"Return", "Intro", "Enter"}, {"Escape", "Esc", "Esc"}, {"Space", "Espacio", "Space"}, {"Backspace", "Retroceso", "Backspace"},
            {"Left Shift", "Shift izq.", "Left Shift"}, {"Right Shift", "Shift der.", "Right Shift"}, {"Left Ctrl", "Ctrl izq.", "Left Ctrl"},
            {"Right Ctrl", "Ctrl der.", "Right Ctrl"}, {"Left Alt", "Alt izq.", "Left Alt"}, {"Right Alt", "Alt der.", "Right Alt"},
            {"Left GUI", "Súper izq.", "Left Super"}, {"Right GUI", "Súper der.", "Right Super"}, {"Up", "Flecha ↑", "Up Arrow"},
            {"Down", "Flecha ↓", "Down Arrow"}, {"Left", "Flecha ←", "Left Arrow"}, {"Right", "Flecha →", "Right Arrow"},
            {"Delete", "Supr", "Delete"}, {"Home", "Inicio", "Home"}, {"End", "Fin", "End"}, {"PageUp", "Re Pág", "Page Up"},
            {"PageDown", "Av Pág", "Page Down"}, {"CapsLock", "Bloq Mayús", "Caps Lock"}};
        for (const auto & Name : Names)
        {
            if (Raw == Name.Sdl)
            {
                return Tx(Name.Es, Name.En);
            }
        }
        if (Raw.startsWith("Keypad "))
        {
            return Tx("Teclado num. ", "Numpad ") + Raw.mid(7);
        }
    }
    return Raw;
}

// One row of the assignments: a capsule that shows the binding (and waits for an input when it is clicked), the button that
// clears it, and the control of the drawing that it stands for (BUTTON of the plugin)
struct ScanRow
{
    BUTTON * Target = nullptr;
    QWidget * Host = nullptr;
    QPushButton * Scan = nullptr;
    QToolButton * Clear = nullptr;
    QLabel * Label = nullptr;
    N64Control Control = N64Control::None;
    bool Capturing = false;
    bool Hot = false; // the pointer is on the row: its clear button shows
    std::function<void(bool)> OnCapturing;

    // The capsule shows what is saved in the BUTTON (property "binding" keeps the technical name of the plugin)
    void Refresh()
    {
        const QString Raw = FromWide(g_InputPlugin->ButtonAssignment(*Target));
        QString Shown;
        if (Capturing)
        {
            Shown = Tx("Pulsa una entrada…", "Press input…");
        }
        else
        {
            Shown = Raw.isEmpty() ? QStringLiteral("—") : PrettyBinding(*Target, Raw);
        }
        Scan->setProperty("binding", Raw);
        Scan->setProperty("empty", Raw.isEmpty());
        Scan->setProperty("capturing", Capturing);
        Scan->setText(Shown);
        Scan->setToolTip(Capturing ? Tx("Pulsa una tecla, un botón del ratón, la rueda o un botón del mando", "Press a key, a mouse button, the wheel or a game pad button")
                                   : Tx("Asignar: pulsa una tecla, un botón del ratón o del mando", "Assign: press a key, a mouse button or a game pad button"));
        Scan->setAccessibleDescription(Capturing ? Tx("Esperando una entrada", "Waiting for an input") : Shown);
        if (Clear != nullptr)
        {
            Clear->setVisible(!Raw.isEmpty() && !Capturing && Hot);
        }
        if (Host != nullptr)
        {
            Host->setProperty("current", Capturing); // the row that waits is the one that stands out
            Repolish(Host);
        }
        Repolish(Scan);
    }
    void SetCapturing(bool On)
    {
        Capturing = On;
        Refresh();
        if (OnCapturing)
        {
            OnCapturing(On);
        }
    }
};

class InputDialog;

// PJ64_INPUT_DEBUG=1: every mouse button, key and context menu event of the application while the input dialog exists,
// with the state of the scan (the events before a row waits for an input are the ones that decide what a user sees)
class DebugLogger : public QObject
{
public:
    DebugLogger(const std::function<bool()> & Scanning, QObject * Parent) :
        QObject(Parent),
        m_Scanning(Scanning)
    {
        qApp->installEventFilter(this);
    }
    ~DebugLogger() override
    {
        qApp->removeEventFilter(this);
    }
    bool eventFilter(QObject * Receiver, QEvent * Event) override
    {
        const char * Name = nullptr;
        switch (Event->type())
        {
        case QEvent::MouseButtonPress: Name = "MousePress"; break;
        case QEvent::MouseButtonRelease: Name = "MouseRelease"; break;
        case QEvent::MouseButtonDblClick: Name = "MouseDblClick"; break;
        case QEvent::KeyPress: Name = "KeyPress"; break;
        case QEvent::KeyRelease: Name = "KeyRelease"; break;
        case QEvent::ContextMenu: Name = "ContextMenu"; break;
        case QEvent::Wheel: Name = "Wheel"; break;
        default: return false;
        }
        QString Detail;
        if (const QMouseEvent * M = dynamic_cast<QMouseEvent *>(Event))
        {
            Detail = QString("button=0x%1 buttons=0x%2").arg((unsigned)M->button(), 0, 16).arg((unsigned)M->buttons(), 0, 16);
        }
        else if (const QKeyEvent * K = dynamic_cast<QKeyEvent *>(Event))
        {
            Detail = QString("qtkey=0x%1 native=%2 mods=0x%3").arg(K->key(), 0, 16).arg(K->nativeScanCode()).arg((unsigned)K->modifiers(), 0, 16);
        }
        if (Receiver->isWidgetType() || strcmp(Receiver->metaObject()->className(), "QWidgetWindow") == 0)
        {
            fprintf(stderr, "[input-idle] %s %s scanning=%d on %s '%s'\n", Name, Detail.toUtf8().constData(), m_Scanning() ? 1 : 0,
                Receiver->metaObject()->className(), Receiver->objectName().toUtf8().constData());
        }
        return false;
    }

private:
    std::function<bool()> m_Scanning;
};

// Shared scan state machine (CScanButton::DetectKey/OnTimer for one row at a time)
class Scanner : public QObject
{
public:
    // Called with the new button (empty button = cleared); the caller removes
    // duplicates and marks its page modified.
    typedef std::function<void(const BUTTON &)> ChangeFn;

    Scanner(QDialog * Dialog) :
        QObject(Dialog),
        m_Dialog(Dialog),
        m_Row(nullptr)
    {
        m_Timer = new QTimer(this);
        m_Timer->setInterval(20);
        connect(m_Timer, &QTimer::timeout, this, [this]() { OnTimer(); });
    }

    ~Scanner() override
    {
        // the dialog is closed while a row waits (window manager, Alt+F4 ...): the plugin must not stay in scan mode
        if (m_Row != nullptr)
        {
            qApp->removeEventFilter(this);
            g_InputPlugin->EndScanDevices();
            m_Row = nullptr;
        }
    }

    bool Scanning() const
    {
        return m_Row != nullptr;
    }

    // A widget that keeps its left click while a row waits (the button that cancels the assignment): every other click is an
    // input to assign
    void SetPassThrough(QWidget * Widget)
    {
        m_PassThrough = Widget;
    }

    // The scan ends without a result (the dialog changes of control, ...)
    void Cancel()
    {
        Stop(true);
    }

    void Start(ScanRow * Row, ChangeFn OnChange, std::function<void(bool)> OnDone)
    {
        Stop(false);
        m_Row = Row;
        m_OnChange = OnChange;
        m_OnDone = OnDone;
        m_Count = 0;
        m_KeyButton = BUTTON();
        m_Captured = false;
        m_Clock.start();
        if (InputDebug())
        {
            fprintf(stderr, "[input] waiting for an input\n");
        }
        qApp->installEventFilter(this);
        g_InputPlugin->StartScanDevices(0);
        m_Timer->start();
        Row->SetCapturing(true);
    }

    // Qt key event while scanning (the dialogs forward their keyPressEvent here; the event filter has taken it already)
    bool KeyPressed(QKeyEvent * Event)
    {
        if (m_Row == nullptr)
        {
            return false;
        }
        CaptureKey(Event);
        return true;
    }

    // Application wide while a row waits: nothing reaches the widgets (the focus button, Escape/Enter of the dialog, the
    // context menu, ...) until the scan ends; the release of the click that captured is swallowed too
    bool eventFilter(QObject * Receiver, QEvent * Event) override
    {
        if (InputDebug() && m_Row != nullptr)
        {
            switch (Event->type())
            {
            case QEvent::KeyPress:
            case QEvent::KeyRelease:
            {
                const QKeyEvent * K = static_cast<QKeyEvent *>(Event);
                fprintf(stderr, "[input] %s qtkey=0x%x native scancode=%u virtualkey=0x%x mods=0x%x repeat=%d text='%s' -> receiver %s\n",
                    Event->type() == QEvent::KeyPress ? "KeyPress" : "KeyRelease", K->key(), K->nativeScanCode(), K->nativeVirtualKey(),
                    (unsigned)K->modifiers(), K->isAutoRepeat() ? 1 : 0, K->text().toUtf8().constData(), Receiver->metaObject()->className());
                break;
            }
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonRelease:
            case QEvent::MouseButtonDblClick:
            {
                const QMouseEvent * M = static_cast<QMouseEvent *>(Event);
                fprintf(stderr, "[input] Mouse%s button=0x%x buttons=0x%x source=%d -> receiver %s\n",
                    Event->type() == QEvent::MouseButtonPress ? "Press" : (Event->type() == QEvent::MouseButtonRelease ? "Release" : "DblClick"),
                    (unsigned)M->button(), (unsigned)M->buttons(), (int)M->source(), Receiver->metaObject()->className());
                break;
            }
            case QEvent::Wheel:
            {
                const QWheelEvent * W = static_cast<QWheelEvent *>(Event);
                fprintf(stderr, "[input] Wheel angle=(%d,%d) pixel=(%d,%d) inverted=%d -> receiver %s\n", W->angleDelta().x(), W->angleDelta().y(),
                    W->pixelDelta().x(), W->pixelDelta().y(), W->inverted() ? 1 : 0, Receiver->metaObject()->className());
                break;
            }
            case QEvent::ContextMenu:
                fprintf(stderr, "[input] ContextMenu -> receiver %s (consumed)\n", Receiver->metaObject()->className());
                break;
            case QEvent::ShortcutOverride:
                fprintf(stderr, "[input] ShortcutOverride (accepted)\n");
                break;
            default:
                break;
            }
        }
        if (m_Row == nullptr)
        {
            return false;
        }
        switch (Event->type())
        {
        case QEvent::ShortcutOverride:
            Event->accept();
            return true;
        case QEvent::KeyPress:
            CaptureKey(static_cast<QKeyEvent *>(Event));
            return true;
        case QEvent::KeyRelease:
            return true;
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        {
            const QMouseEvent * Mouse = static_cast<QMouseEvent *>(Event);
            if (OverPassThrough(Mouse))
            {
                return false;
            }
            const int Number = SdlMouseButton(Mouse->button());
            if (Number >= 0 && !m_Captured)
            {
                SetCaptured(Number);
            }
            return true;
        }
        case QEvent::MouseButtonRelease:
            return !OverPassThrough(static_cast<QMouseEvent *>(Event));
        case QEvent::ContextMenu:
            return true;
        case QEvent::Wheel:
        {
            const QWheelEvent * Wheel = static_cast<QWheelEvent *>(Event);
            const QPoint Delta = Wheel->angleDelta();
            int Number = -1;
            if (qAbs(Delta.y()) >= qAbs(Delta.x()))
            {
                Number = Delta.y() > 0 ? 32 : (Delta.y() < 0 ? 33 : -1);
            }
            else
            {
                // (Qt: a positive horizontal angle is the wheel pushed to the left, X button 6; SDL's x < 0 is left too)
                Number = Delta.x() > 0 ? 34 : 35;
            }
            if (Number >= 0 && !m_Captured)
            {
                SetCaptured(Number);
            }
            return true;
        }
        default:
            break;
        }
        return false;
    }

private:
    bool OverPassThrough(const QMouseEvent * Mouse) const
    {
        if (m_PassThrough == nullptr || !m_PassThrough->isVisible() || Mouse->button() != Qt::LeftButton)
        {
            return false;
        }
        // (where the pointer is, in the widget's own coordinates: nothing covers the button while a row waits)
        return m_PassThrough->rect().contains(m_PassThrough->mapFromGlobal(Mouse->globalPosition().toPoint()));
    }

    void CaptureKey(QKeyEvent * Event)
    {
        Event->accept();
        if (Event->isAutoRepeat() || m_Captured)
        {
            return;
        }
        const int Scancode = SdlScancodeFromEvent(Event);
        if (InputDebug())
        {
            fprintf(stderr, "[input]   -> SDL scancode %d (%s)%s\n", Scancode, Scancode != 0 ? SDL_GetScancodeName((SDL_Scancode)Scancode) : "none", Scancode == 0 ? ": ignored, SDL has no code for this key" : "");
        }
        if (Scancode == 0)
        {
            return; // a key SDL has no code for (media keys the X server does not number...): keep waiting
        }
        m_KeyButton = BUTTON();
        m_KeyButton.Offset = (uint16_t)Scancode;
        m_KeyButton.AxisID = 0;
        m_KeyButton.BtnType = BTNTYPE_KEYBUTTON;
        m_KeyButton.DeviceGuid = g_InputPlugin->KeyboardGuid();
        m_KeyButton.Device = nullptr;
        m_Captured = true;
    }

    void SetCaptured(int MouseNumber)
    {
        if (InputDebug())
        {
            fprintf(stderr, "[input]   -> mouse button number %d captured\n", MouseNumber);
        }
        m_KeyButton = BUTTON();
        m_KeyButton.Offset = (uint16_t)MouseNumber;
        m_KeyButton.AxisID = 0;
        m_KeyButton.BtnType = BTNTYPE_MOUSEBUTTON;
        m_KeyButton.DeviceGuid = g_InputPlugin->MouseGuid();
        m_KeyButton.Device = nullptr;
        m_Captured = true;
    }

    void OnTimer()
    {
        if (m_Row == nullptr)
        {
            return;
        }
        BUTTON Empty = {};
        BUTTON Button = *m_Row->Target;
        bool Stop = false, Success = false;
        CSdlInput::ScanResult Result = CSdlInput::SCAN_FAILED;
        if (m_Captured)
        {
            Button = m_KeyButton;
            Result = CSdlInput::SCAN_SUCCEED;
        }
        else
        {
            Result = g_InputPlugin->ScanDevices(Button);
            if (InputDebug() && Result != CSdlInput::SCAN_FAILED)
            {
                fprintf(stderr, "[input] SDL polling (no Qt event) result=%d type=%d offset=%u -> '%s'\n", (int)Result, (int)Button.BtnType, (unsigned)Button.Offset,
                    FromWide(g_InputPlugin->ButtonAssignment(Button)).toUtf8().constData());
            }
        }
        if (Result == CSdlInput::SCAN_SUCCEED && !SameButton(Button, *m_Row->Target))
        {
            if (m_OnChange)
            {
                m_OnChange(Button);
            }
            *m_Row->Target = Button;
        }
        if (Result == CSdlInput::SCAN_ESCAPE && !IsEmptyButton(*m_Row->Target))
        {
            if (m_OnChange)
            {
                m_OnChange(Empty);
            }
            *m_Row->Target = Empty;
        }
        if (Result == CSdlInput::SCAN_SUCCEED || Result == CSdlInput::SCAN_ESCAPE)
        {
            Success = Result == CSdlInput::SCAN_SUCCEED;
            Stop = true;
        }
        if ((m_Count % 30) == 0)
        {
            const int Left = 10 - (int)(m_Clock.elapsed() / 1000);
            if (Left > 0)
            {
                m_Dialog->setWindowTitle(m_Dialog->property("baseTitle").toString() + QString(": ") + Tx("pulsa una tecla o botón… (%1 s)", "press a key or button… (%1 s)").arg(Left));
            }
            else
            {
                if (m_OnChange)
                {
                    m_OnChange(Empty);
                }
                *m_Row->Target = Empty;
                Stop = true;
            }
        }
        if (m_Count > 500)
        {
            Stop = true;
        }
        m_Count++;
        if (Stop)
        {
            Finish(Success);
        }
    }

    void Finish(bool Success)
    {
        ScanRow * Row = m_Row;
        std::function<void(bool)> Done = m_OnDone;
        Stop(true);
        if (InputDebug())
        {
            fprintf(stderr, "[input] finished: success=%d, the row shows '%s'\n", Success ? 1 : 0, Row->Scan->text().toUtf8().constData());
        }
        if (Done)
        {
            Done(Success);
        }
    }

    void Stop(bool Restore)
    {
        if (m_Row == nullptr)
        {
            return;
        }
        m_Timer->stop();
        qApp->removeEventFilter(this);
        if (Restore)
        {
            m_Dialog->setWindowTitle(m_Dialog->property("baseTitle").toString());
        }
        g_InputPlugin->EndScanDevices();
        ScanRow * Row = m_Row;
        m_Row = nullptr;
        Row->SetCapturing(false); // (the capsule shows the binding again, the drawing loses its capturing control)
    }

    QDialog * m_Dialog;
    QTimer * m_Timer;
    ScanRow * m_Row;
    ChangeFn m_OnChange;
    std::function<void(bool)> m_OnDone;
    QElapsedTimer m_Clock;
    QWidget * m_PassThrough = nullptr;
    int m_Count = 0;
    BUTTON m_KeyButton = {};
    bool m_Captured = false;
};

// ShortcutsUI.cpp: CShortcutsDlg (Lock/Unlock Mouse)
class ShortcutsDialog : public QDialog
{
public:
    explicit ShortcutsDialog(QWidget * Parent) :
        QDialog(Parent),
        m_Shortcuts(g_InputPlugin->Shortcuts())
    {
        setWindowTitle(Tx("Atajos", "Shortcuts"));
        setProperty("baseTitle", windowTitle());
        m_Scanner = new Scanner(this);
        QVBoxLayout * Layout = new QVBoxLayout(this);
        QHBoxLayout * Row = new QHBoxLayout;
        m_Row.Label = new QLabel(Tx("Bloquear/desbloquear el ratón:", "Lock/Unlock Mouse:"), this);
        m_Row.Scan = new QPushButton(this);
        m_Row.Scan->setObjectName("VxScan");
        m_Row.Scan->setProperty("vx", "binding");
        m_Row.Scan->setMinimumWidth(170);
        m_Row.Target = &m_Shortcuts.LOCKMOUSE;
        Row->addWidget(m_Row.Label);
        Row->addWidget(m_Row.Scan, 1);
        Layout->addLayout(Row);
        QDialogButtonBox * Buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        Layout->addWidget(Buttons);
        m_Row.Refresh();
        connect(m_Row.Scan, &QPushButton::clicked, this, [this]() { m_Scanner->Start(&m_Row, nullptr, nullptr); });
        connect(Buttons, &QDialogButtonBox::accepted, this, [this]() {
            g_InputPlugin->Shortcuts() = m_Shortcuts;
            g_InputPlugin->SaveShortcuts();
            accept();
        });
        connect(Buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

protected:
    void keyPressEvent(QKeyEvent * Event) override
    {
        if (m_Scanner->KeyPressed(Event))
        {
            return;
        }
        QDialog::keyPressEvent(Event);
    }

private:
    SHORTCUTS m_Shortcuts;
    ScanRow m_Row;
    Scanner * m_Scanner;
};

// Cards laid out in columns by the width they get: two columns when two cards fit side by side (the assignment fits in one
// screen), one when they do not. It is a layout rule (the minimum width of the widest card), not a size: the page needs no
// horizontal scroll at any size or scale.
class CardGrid : public QWidget
{
public:
    explicit CardGrid(QWidget * Parent) :
        QWidget(Parent)
    {
        m_Grid = new QGridLayout(this);
        m_Grid->setContentsMargins(0, 0, 8, 0);
        m_Grid->setHorizontalSpacing(12);
        m_Grid->setVerticalSpacing(10);
    }
    void AddCard(QWidget * Card)
    {
        m_Cards.push_back(Card);
        Reflow(true);
    }
    int Columns() const
    {
        return m_Columns;
    }
    // The page never asks for more width than one column needs: the scroll area gives it the viewport (no horizontal scroll),
    // and the cards are rearranged to the width they get
    QSize minimumSizeHint() const override
    {
        return QSize(NeededWidth(), m_Grid->minimumSize().height());
    }

protected:
    void resizeEvent(QResizeEvent * Event) override
    {
        QWidget::resizeEvent(Event);
        Reflow(false);
    }

private:
    int NeededWidth() const
    {
        int Need = 0;
        for (QWidget * Card : m_Cards)
        {
            Need = qMax(Need, Card->minimumSizeHint().width());
        }
        const QMargins M = m_Grid->contentsMargins();
        return Need + M.left() + M.right();
    }
    void Reflow(bool Force)
    {
        const int Columns = (m_Cards.size() > 1 && width() >= 2 * NeededWidth() + m_Grid->horizontalSpacing()) ? 2 : 1;
        if (!Force && Columns == m_Columns)
        {
            return;
        }
        m_Columns = Columns;
        for (QWidget * Card : m_Cards)
        {
            m_Grid->removeWidget(Card);
        }
        for (int i = 0; i < (int)m_Cards.size() + 1; i++)
        {
            m_Grid->setRowStretch(i, 0);
        }
        for (int i = 0; i < (int)m_Cards.size(); i++)
        {
            m_Grid->addWidget(m_Cards[i], i / Columns, i % Columns, Qt::AlignTop);
        }
        m_Grid->setColumnStretch(0, 1);
        m_Grid->setColumnStretch(1, Columns == 2 ? 1 : 0);
        m_Grid->setRowStretch(((int)m_Cards.size() + Columns - 1) / Columns, 1);
    }

    QGridLayout * m_Grid;
    std::vector<QWidget *> m_Cards;
    int m_Columns = 0;
};

QScrollArea * MakeScroll(CardGrid * Grid, const char * Name, QWidget * Parent)
{
    QScrollArea * Scroll = new QScrollArea(Parent);
    Scroll->setObjectName(Name);
    Scroll->setWidgetResizable(true);
    Scroll->setFrameShape(QFrame::NoFrame);
    Scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    Scroll->setWidget(Grid);
    Grid->setAutoFillBackground(false);
    Scroll->viewport()->setAutoFillBackground(false);
    return Scroll;
}

// InputConfigUI.cpp: CControllerSettings, laid out as the tabs Assignment / Settings / Test: the assignment is a compact grid of
// cards, the settings hold the pak and the sliders, the test has the big drawing of the controller (one per page).
class ControllerPage : public QWidget
{
public:
    ControllerPage(uint32_t Number, InputDialog * Owner, Scanner * Scan, QWidget * Parent);
    ~ControllerPage() override;
    void Apply();
    void RemoveMapping(const BUTTON & Button);
    void RefreshBoundDevice();
    void ResetDefaults();
    int Pak() const
    {
        return m_ControlInfo.Plugin;
    }
    void SetPak(int Pak)
    {
        if (m_ControlInfo.Plugin != Pak)
        {
            m_ControlInfo.Plugin = Pak;
            SetModified();
        }
    }
    bool Modified = false;

    N64CONTROLLER m_Controller;

    // The page is the one that is shown: it owns the keys of the test now
    void Activate();
    void Deactivate();

protected:
    bool eventFilter(QObject * Watched, QEvent * Event) override;

private:
    ScanRow * MakeRow(const QString & Text, BUTTON & Target, N64Control Control);
    QFrame * MakeCard(const QString & Title, QWidget * Parent);
    void AddRowTo(QFrame * Card, int Index);
    void DisplayController();
    void EnablePage(int32_t Present);
    void SetModified();
    void SetupNext();
    void EndSequence();
    void UpdateAssignUi();
    void UpdateTest();
    void ClearRow(ScanRow * Row);
    void SetHot(ScanRow * Row, bool On);
    void RefreshRows();
    void RefreshStatus();

public:
    // The test page follows the keys of the whole application: only the page that shows it may do so
    bool IsTesting() const
    {
        return m_Tabs->currentIndex() == 2;
    }
    void StartTest()
    {
        m_KeysDown.clear();
        m_MouseDown.clear();
        qApp->installEventFilter(this);
        m_TestTimer->start();
    }
    void StopTest()
    {
        qApp->removeEventFilter(this);
        m_TestTimer->stop();
        m_Pad->ClearPressed();
    }

private:
    uint32_t m_Number;
    InputDialog * m_Owner;
    Scanner * m_Scanner;
    N64ControllerWidget * m_Pad = nullptr;
    bool m_Active = false;
    CONTROL m_ControlInfo;
    std::vector<ScanRow *> m_Rows; // in the order of the plugin (the tests find the rows by it)
    std::vector<int> m_Sequence;   // the order of the sequential assignment: the order of the cards
    QComboBox * m_DeviceType;
    QLabel * m_Status;
    QLabel * m_BoundDevice;
    QTabWidget * m_Tabs;
    QScrollArea * m_AssignScroll = nullptr;
    QLabel * m_TestInfo;
    QLabel * m_TestStick;
    QSlider * m_Range;
    QSlider * m_DeadZone;
    QSlider * m_Sensitivity;
    QLabel * m_RangeLabel;
    QLabel * m_DeadZoneLabel;
    QLabel * m_SensitivityLabel;
    QCheckBox * m_RealRange;
    QCheckBox * m_RemoveDuplicate;
    QPushButton * m_SetupBtn;
    QPushButton * m_CancelBtn;
    QLabel * m_SequenceStatus;
    QPushButton * m_DefaultsBtn;
    QButtonGroup * m_PakGroup;
    QTimer * m_TestTimer;
    std::vector<QWidget *> m_ControllerOnly;
    std::set<int> m_KeysDown, m_MouseDown;
    int m_SetupIndex = -1;
};

class InputDialog : public QDialog
{
public:
    explicit InputDialog(QWidget * Parent) :
        QDialog(Parent)
    {
        setObjectName("InputDialog");
        setWindowTitle("Vortex64 - Input");
        setProperty("baseTitle", windowTitle());
        setMinimumSize(640, 480);
        QSize Size(1040, 700);
        if (QScreen * Screen = QGuiApplication::primaryScreen())
        {
            const QRect Area = Screen->availableGeometry();
            Size = Size.boundedTo(QSize(Area.width() * 9 / 10, Area.height() * 9 / 10));
            // (a work area smaller than the usual minimum: the dialog still fits it, the page scrolls)
            setMinimumSize(minimumSize().boundedTo(QSize(Area.width() * 95 / 100, Area.height() * 95 / 100)));
        }
        resize(Size.expandedTo(minimumSize()));
        m_Scanner = new Scanner(this);
        if (InputDebug())
        {
            new DebugLogger([this]() { return m_Scanner->Scanning(); }, this);
        }

        m_Stack = new QStackedWidget(this);
        for (uint32_t i = 0; i < 4; i++)
        {
            ControllerPage * Page = new ControllerPage(i, this, m_Scanner, m_Stack);
            m_Pages.push_back(Page);
            m_Stack->addWidget(Page);
        }

        // the top bar: which control is being edited
        QWidget * Top = new QWidget(this);
        Top->setProperty("vx", "toolbar");
        Top->setAttribute(Qt::WA_StyledBackground, true);
        QHBoxLayout * TopLayout = new QHBoxLayout(Top);
        TopLayout->setContentsMargins(18, 10, 18, 4);
        TopLayout->setSpacing(6);
        m_Title = new QLabel(Top);
        m_Title->setProperty("vx", "title");
        TopLayout->addWidget(m_Title);
        TopLayout->addSpacing(14);
        m_ControlGroup = new QButtonGroup(this);
        m_ControlGroup->setExclusive(true);
        for (int i = 0; i < 4; i++)
        {
            QPushButton * B = new QPushButton(Tx("Control ", "Control ") + QString::number(i + 1), Top);
            B->setObjectName(QString("VxControl%1").arg(i + 1));
            B->setProperty("vx", "segment");
            B->setCheckable(true);
            B->setCursor(Qt::PointingHandCursor);
            m_ControlGroup->addButton(B, i);
            TopLayout->addWidget(B);
        }
        TopLayout->addStretch(1);

        QPushButton * ResetButton = new QPushButton(Tx("Restablecer", "Reset"), this);
        ResetButton->setObjectName("VxInputReset");
        QPushButton * OkButton = new QPushButton(Tx("Aceptar", "OK"), this);
        OkButton->setObjectName("VxOk");
        OkButton->setDefault(true);
        QPushButton * CancelButton = new QPushButton(Tx("Cancelar", "Cancel"), this);
        CancelButton->setObjectName("VxCancel");
        QPushButton * ApplyButton = new QPushButton(Tx("Aplicar", "Apply"), this);
        ApplyButton->setObjectName("VxApply");
        QWidget * Footer = new QWidget(this);
        Footer->setProperty("vx", "footer");
        Footer->setAttribute(Qt::WA_StyledBackground, true);
        QHBoxLayout * FooterLayout = new QHBoxLayout(Footer);
        FooterLayout->setContentsMargins(16, 8, 16, 8);
        FooterLayout->setSpacing(8);
        FooterLayout->addWidget(ResetButton);
        FooterLayout->addStretch(1);
        FooterLayout->addWidget(OkButton);
        FooterLayout->addWidget(CancelButton);
        FooterLayout->addWidget(ApplyButton);

        QVBoxLayout * Layout = new QVBoxLayout(this);
        Layout->setContentsMargins(0, 0, 0, 0);
        Layout->setSpacing(0);
        Layout->addWidget(Top);
        QHBoxLayout * Body = new QHBoxLayout;
        Body->setContentsMargins(18, 0, 18, 10);
        Body->addWidget(m_Stack);
        Layout->addLayout(Body, 1);
        Layout->addWidget(Footer);

        connect(m_ControlGroup, &QButtonGroup::idClicked, this, [this](int Row) { ShowControl(Row); });
        connect(OkButton, &QPushButton::clicked, this, [this]() {
            ApplyAll();
            accept();
        });
        connect(CancelButton, &QPushButton::clicked, this, &QDialog::reject);
        connect(ApplyButton, &QPushButton::clicked, this, [this]() { ApplyAll(); });
        connect(ResetButton, &QPushButton::clicked, this, [this]() { m_Pages[m_Current]->ResetDefaults(); });
        ShowControl(0);
    }

    void RemoveMapping(const BUTTON & Button)
    {
        for (ControllerPage * Page : m_Pages)
        {
            Page->RemoveMapping(Button);
        }
    }

protected:
    void keyPressEvent(QKeyEvent * Event) override
    {
        if (m_Scanner->KeyPressed(Event))
        {
            return;
        }
        QDialog::keyPressEvent(Event);
    }

private:
    void ShowControl(int Row)
    {
        if (Row < 0 || Row >= (int)m_Pages.size())
        {
            return;
        }
        // (a row that waits for an input does not follow the control: the scan ends)
        m_Scanner->Cancel();
        for (ControllerPage * Page : m_Pages)
        {
            Page->Deactivate();
        }
        m_Current = Row;
        m_Stack->setCurrentIndex(Row);
        if (QAbstractButton * Segment = m_ControlGroup->button(Row))
        {
            Segment->setChecked(true);
        }
        m_Title->setText(Tx("Input", "Input"));
        m_Pages[Row]->Activate();
    }
    void ApplyAll()
    {
        for (ControllerPage * Page : m_Pages)
        {
            if (Page->Modified)
            {
                Page->Apply();
                Page->Modified = false;
            }
        }
    }

    QStackedWidget * m_Stack;
    QLabel * m_Title;
    QButtonGroup * m_ControlGroup;
    Scanner * m_Scanner;
    std::vector<ControllerPage *> m_Pages;
    int m_Current = 0;
};

ControllerPage::ControllerPage(uint32_t Number, InputDialog * Owner, Scanner * Scan, QWidget * Parent) :
    QWidget(Parent),
    m_Controller(g_InputPlugin->Controllers(Number)),
    m_Number(Number),
    m_Owner(Owner),
    m_Scanner(Scan),
    m_ControlInfo(g_InputPlugin->ControlInfo(Number))
{
    QVBoxLayout * Main = new QVBoxLayout(this);
    Main->setContentsMargins(0, 0, 0, 0);
    Main->setSpacing(4);

    // the device of the control and its state, in one line
    QHBoxLayout * Top = new QHBoxLayout;
    Top->setSpacing(8);
    QLabel * DeviceText = new QLabel(Tx("Dispositivo", "Device"), this);
    m_DeviceType = new QComboBox(this);
    m_DeviceType->setObjectName("VxInputDevice");
    m_DeviceType->addItem(Tx("Ninguno", "None"), (int)PRESENT_NONE);
    m_DeviceType->addItem(Tx("Mando N64", "N64 Controller"), (int)PRESENT_CONT);
    m_DeviceType->addItem(Tx("Ratón N64", "N64 Mouse"), (int)PRESENT_MOUSE);
    QLabel * StatusText = new QLabel(Tx("Estado", "Status"), this);
    m_Status = new QLabel(this);
    m_Status->setObjectName("VxInputStatus");
    m_BoundDevice = new QLabel(this);
    m_BoundDevice->setProperty("vx", "muted");
    m_BoundDevice->setMinimumWidth(0);
    m_BoundDevice->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    Top->addWidget(DeviceText);
    Top->addWidget(m_DeviceType);
    Top->addSpacing(14);
    Top->addWidget(StatusText);
    Top->addWidget(m_Status);
    Top->addSpacing(8);
    Top->addWidget(m_BoundDevice, 1);
    Main->addLayout(Top);

    // the rows, in the order of the plugin (the tests find them by it)
    MakeRow(Tx("D-Pad ↑", "D-Pad ↑"), m_Controller.U_DPAD, N64Control::DPadUp);
    MakeRow(Tx("D-Pad ↓", "D-Pad ↓"), m_Controller.D_DPAD, N64Control::DPadDown);
    MakeRow(Tx("D-Pad ←", "D-Pad ←"), m_Controller.L_DPAD, N64Control::DPadLeft);
    MakeRow(Tx("D-Pad →", "D-Pad →"), m_Controller.R_DPAD, N64Control::DPadRight);
    MakeRow(Tx("Stick ↑", "Stick ↑"), m_Controller.U_ANALOG, N64Control::StickUp);
    MakeRow(Tx("Stick ↓", "Stick ↓"), m_Controller.D_ANALOG, N64Control::StickDown);
    MakeRow(Tx("Stick ←", "Stick ←"), m_Controller.L_ANALOG, N64Control::StickLeft);
    MakeRow(Tx("Stick →", "Stick →"), m_Controller.R_ANALOG, N64Control::StickRight);
    MakeRow("C ↑", m_Controller.U_CBUTTON, N64Control::CUp);
    MakeRow("C ↓", m_Controller.D_CBUTTON, N64Control::CDown);
    MakeRow("C ←", m_Controller.L_CBUTTON, N64Control::CLeft);
    MakeRow("C →", m_Controller.R_CBUTTON, N64Control::CRight);
    MakeRow("B", m_Controller.B_BUTTON, N64Control::B);
    MakeRow("A", m_Controller.A_BUTTON, N64Control::A);
    MakeRow("Start", m_Controller.START_BUTTON, N64Control::Start);
    MakeRow("Z", m_Controller.Z_TRIG, N64Control::Z);
    MakeRow("L", m_Controller.L_TRIG, N64Control::L);
    MakeRow("R", m_Controller.R_TRIG, N64Control::R);

    m_Tabs = new QTabWidget(this);
    m_Tabs->setObjectName("VxInputTabs");

    // ---- Assignment: two columns of compact cards (one column when the width does not allow two)
    QWidget * Assign = new QWidget;
    QVBoxLayout * AssignLayout = new QVBoxLayout(Assign);
    AssignLayout->setContentsMargins(0, 8, 0, 0);
    AssignLayout->setSpacing(8);
    CardGrid * Cards = new CardGrid(Assign);
    Cards->setObjectName("VxInputCards");
    const struct
    {
        const char * Es;
        const char * En;
        std::vector<int> Rows;
    } Groups[] = {
        {"MOVIMIENTO", "MOVEMENT", {4, 5, 6, 7}},
        {"BOTONES", "BUTTONS", {13, 12, 15, 14}},
        {"C-BUTTONS", "C-BUTTONS", {8, 9, 10, 11}},
        {"D-PAD", "D-PAD", {0, 1, 2, 3}},
        {"HOMBROS", "SHOULDERS", {16, 17}},
    };
    for (const auto & Group : Groups)
    {
        QFrame * Card = MakeCard(Tx(Group.Es, Group.En), Cards);
        for (int Index : Group.Rows)
        {
            AddRowTo(Card, Index);
            m_Sequence.push_back(Index);
        }
        Cards->AddCard(Card);
    }
    m_AssignScroll = MakeScroll(Cards, "VxInputScroll", Assign);
    AssignLayout->addWidget(m_AssignScroll, 1);
    QHBoxLayout * Quick = new QHBoxLayout;
    Quick->setSpacing(8);
    m_SetupBtn = new QPushButton(Tx("Asignar en secuencia", "Assign sequentially"), Assign);
    m_SetupBtn->setObjectName("VxInputSetup");
    m_CancelBtn = new QPushButton(Tx("Cancelar asignación", "Cancel assignment"), Assign);
    m_CancelBtn->setObjectName("VxInputCancelAssign");
    m_CancelBtn->setVisible(false);
    m_SequenceStatus = new QLabel(Assign);
    m_SequenceStatus->setObjectName("VxInputSequence");
    m_SequenceStatus->setProperty("vx", "muted");
    m_SequenceStatus->setVisible(false);
    m_DefaultsBtn = new QPushButton(Tx("Valores por defecto", "Defaults"), Assign);
    m_DefaultsBtn->setObjectName("VxInputDefaults");
    m_DefaultsBtn->setProperty("vx", "ghost");
    Quick->addWidget(m_SetupBtn);
    Quick->addWidget(m_CancelBtn);
    Quick->addWidget(m_SequenceStatus, 1);
    Quick->addStretch(1);
    Quick->addWidget(m_DefaultsBtn);
    AssignLayout->addLayout(Quick);
    m_Tabs->addTab(Assign, Tx("Asignación", "Assignment"));

    // ---- Settings: the pak, the stick, the mouse and the general options, in the same grid of cards
    CardGrid * Options = new CardGrid(m_Tabs);
    Options->setObjectName("VxSettingsCards");
    auto Slider = [&](QFrame * Card, const QString & Title, QSlider *& S, QLabel *& Value, const char * Id) {
        QHBoxLayout * Row = new QHBoxLayout;
        QLabel * Text = new QLabel(Title, Card);
        Text->setProperty("vx", "rowlabel");
        Text->setMinimumWidth(112);
        S = new QSlider(Qt::Horizontal, Card);
        S->setObjectName(Id);
        S->setRange(1, 100);
        S->setMinimumWidth(110);
        Value = new QLabel(Card);
        Value->setProperty("vx", "rowlabel");
        Value->setMinimumWidth(40);
        Value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        Row->addWidget(Text);
        Row->addWidget(S, 1);
        Row->addWidget(Value);
        static_cast<QVBoxLayout *>(Card->layout())->addLayout(Row);
    };
    QFrame * PakCard = MakeCard(Tx("PAK", "PAK"), Options);
    m_PakGroup = new QButtonGroup(this);
    m_PakGroup->setExclusive(true);
    {
        QHBoxLayout * PakLayout = new QHBoxLayout;
        PakLayout->setSpacing(6);
        const struct
        {
            const char * Es;
            const char * En;
            int Value;
            const char * Id;
        } Paks[] = {{"Sin pak", "No pak", (int)PLUGIN_NONE, "VxPakNone"}, {"Memory Pak", "Memory Pak", (int)PLUGIN_MEMPAK, "VxPakMemory"}, {"Rumble Pak", "Rumble Pak", (int)PLUGIN_RUMBLE_PAK, "VxPakRumble"}};
        for (const auto & Pak : Paks)
        {
            QPushButton * B = new QPushButton(Tx(Pak.Es, Pak.En), PakCard);
            B->setObjectName(Pak.Id);
            B->setCheckable(true);
            B->setProperty("vx", "segment");
            B->setCursor(Qt::PointingHandCursor);
            m_PakGroup->addButton(B, Pak.Value);
            PakLayout->addWidget(B, 1);
        }
        static_cast<QVBoxLayout *>(PakCard->layout())->addLayout(PakLayout);
        QAbstractButton * Current = m_PakGroup->button(m_ControlInfo.Plugin);
        if (Current == nullptr)
        {
            Current = m_PakGroup->button((int)PLUGIN_NONE);
        }
        Current->setChecked(true);
    }
    QFrame * StickCard = MakeCard(Tx("STICK ANALÓGICO", "ANALOG STICK"), Options);
    Slider(StickCard, Tx("Rango", "Range"), m_Range, m_RangeLabel, "VxInputRange");
    Slider(StickCard, Tx("Zona muerta", "Dead zone"), m_DeadZone, m_DeadZoneLabel, "VxInputDeadZone");
    m_RealRange = new QCheckBox(Tx("Rango real del N64", "Real N64 range"), StickCard);
    m_RealRange->setObjectName("VxInputRealRange");
    StickCard->layout()->addWidget(m_RealRange);
    QFrame * MouseCard = MakeCard(Tx("RATÓN N64", "N64 MOUSE"), Options);
    Slider(MouseCard, Tx("Sensibilidad", "Sensitivity"), m_Sensitivity, m_SensitivityLabel, "VxInputSensitivity");
    QFrame * GeneralCard = MakeCard(Tx("GENERAL", "GENERAL"), Options);
    m_RemoveDuplicate = new QCheckBox(Tx("Quitar asignaciones duplicadas", "Remove duplicate mappings"), GeneralCard);
    m_RemoveDuplicate->setObjectName("VxInputRemoveDuplicate");
    QPushButton * Shortcuts = new QPushButton(Tx("Atajos…", "Shortcuts…"), GeneralCard);
    Shortcuts->setObjectName("VxInputShortcuts");
    GeneralCard->layout()->addWidget(m_RemoveDuplicate);
    GeneralCard->layout()->addWidget(Shortcuts);
    Options->AddCard(PakCard);
    Options->AddCard(StickCard);
    Options->AddCard(MouseCard);
    Options->AddCard(GeneralCard);
    QWidget * Settings = new QWidget;
    QVBoxLayout * SettingsLayout = new QVBoxLayout(Settings);
    SettingsLayout->setContentsMargins(0, 8, 0, 0);
    SettingsLayout->addWidget(MakeScroll(Options, "VxSettingsScroll", Settings), 1);
    m_Tabs->addTab(Settings, Tx("Ajustes", "Settings"));

    // ---- Test: the big controller; what is pressed lights up and the stick moves
    QWidget * Test = new QWidget;
    QVBoxLayout * TestLayout = new QVBoxLayout(Test);
    TestLayout->setContentsMargins(0, 8, 0, 0);
    TestLayout->setSpacing(6);
    m_Pad = new N64ControllerWidget(Test);
    m_Pad->setObjectName("VxController");
    m_Pad->setAccessibleName(Tx("Mando N64", "N64 controller"));
    m_TestStick = new QLabel(Test);
    m_TestStick->setObjectName("VxTestStick");
    m_TestStick->setProperty("vx", "heading");
    m_TestStick->setAlignment(Qt::AlignCenter);
    m_TestInfo = new QLabel(Test);
    m_TestInfo->setObjectName("VxTestInfo");
    m_TestInfo->setProperty("vx", "muted");
    m_TestInfo->setWordWrap(true);
    m_TestInfo->setMinimumHeight(m_TestInfo->fontMetrics().lineSpacing() * 2);
    m_TestInfo->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    TestLayout->addWidget(m_Pad, 1);
    TestLayout->addWidget(m_TestStick);
    TestLayout->addWidget(m_TestInfo);
    m_Tabs->addTab(Test, Tx("Prueba", "Test"));
    Main->addWidget(m_Tabs, 1);

    m_TestTimer = new QTimer(this);
    m_TestTimer->setInterval(33);
    connect(m_TestTimer, &QTimer::timeout, this, [this]() { UpdateTest(); });

    for (ScanRow * Row : m_Rows)
    {
        connect(Row->Scan, &QPushButton::clicked, this, [this, Row]() {
            m_SetupIndex = -1;
            m_Scanner->Start(
                Row, [this](const BUTTON & Button) { m_Owner->RemoveMapping(Button); RefreshBoundDevice(); SetModified(); }, [this](bool) { RefreshBoundDevice(); });
        });
        connect(Row->Clear, &QToolButton::clicked, this, [this, Row]() { ClearRow(Row); });
        m_ControllerOnly.push_back(Row->Scan);
        m_ControllerOnly.push_back(Row->Clear);
        // the row that the pointer is on shows its clear button
        Row->Host->installEventFilter(this);
    }
    m_ControllerOnly.push_back(m_Range);
    m_ControllerOnly.push_back(m_DeadZone);
    m_ControllerOnly.push_back(m_SetupBtn);

    connect(m_Range, &QSlider::valueChanged, this, [this]() {
        m_RangeLabel->setText(QString("%1%").arg(m_Range->value()));
        SetModified();
    });
    connect(m_DeadZone, &QSlider::valueChanged, this, [this]() {
        m_DeadZoneLabel->setText(QString("%1%").arg(m_DeadZone->value()));
        SetModified();
    });
    connect(m_Sensitivity, &QSlider::valueChanged, this, [this]() {
        m_SensitivityLabel->setText(QString("%1%").arg(m_Sensitivity->value()));
        m_Controller.Sensitivity = (uint8_t)m_Sensitivity->value();
        SetModified();
    });
    connect(m_RealRange, &QCheckBox::clicked, this, [this](bool On) {
        m_Controller.RealN64Range = On;
        SetModified();
    });
    connect(m_RemoveDuplicate, &QCheckBox::clicked, this, [this](bool On) {
        m_Controller.RemoveDuplicate = On;
        SetModified();
    });
    connect(m_PakGroup, &QButtonGroup::idClicked, this, [this](int Value) { SetPak(Value); });
    connect(m_DeviceType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        SetModified();
        EnablePage(m_DeviceType->currentData().toInt());
    });
    connect(m_DefaultsBtn, &QPushButton::clicked, this, [this]() { ResetDefaults(); });
    connect(m_SetupBtn, &QPushButton::clicked, this, [this]() {
        m_SetupIndex = 0;
        SetupNext();
    });
    connect(m_CancelBtn, &QPushButton::clicked, this, [this]() {
        m_SetupIndex = -1;
        m_Scanner->Cancel();
        EndSequence();
    });
    connect(Shortcuts, &QPushButton::clicked, this, [this]() { ShortcutsDialog(this).exec(); });
    connect(m_Tabs, &QTabWidget::currentChanged, this, [this](int Index) {
        // (a row that waits for an input does not follow the tab: the scan ends, and so does the sequence)
        m_Scanner->Cancel();
        EndSequence();
        if (!m_Active)
        {
            return;
        }
        if (Index == 2)
        {
            StartTest();
        }
        else
        {
            StopTest();
        }
    });

    DisplayController();
    EnablePage(m_DeviceType->currentData().toInt());
}

ControllerPage::~ControllerPage()
{
    // (the widgets of the rows belong to Qt; the rows themselves, with their callbacks, belong to the page)
    for (ScanRow * Row : m_Rows)
    {
        delete Row;
    }
}

QFrame * ControllerPage::MakeCard(const QString & Title, QWidget * Parent)
{
    QFrame * Card = new QFrame(Parent);
    Card->setProperty("vx", "card");
    QVBoxLayout * Layout = new QVBoxLayout(Card);
    Layout->setContentsMargins(12, 8, 12, 8);
    Layout->setSpacing(2);
    QLabel * Head = new QLabel(Title, Card);
    Head->setProperty("vx", "cardtitle");
    Layout->addWidget(Head);
    return Card;
}

// A row: the name of the input, the capsule with its assignment (a click waits for an input) and the button to clear it
void ControllerPage::AddRowTo(QFrame * Card, int Index)
{
    ScanRow * Row = m_Rows[Index];
    Row->Host->setParent(Card);
    static_cast<QVBoxLayout *>(Card->layout())->addWidget(Row->Host);
}

ScanRow * ControllerPage::MakeRow(const QString & Text, BUTTON & Target, N64Control Control)
{
    ScanRow * Entry = new ScanRow;
    Entry->Target = &Target;
    Entry->Control = Control;
    Entry->Host = new QWidget(this);
    Entry->Host->setProperty("vx", "row");
    Entry->Host->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout * Line = new QHBoxLayout(Entry->Host);
    Line->setContentsMargins(6, 2, 2, 2);
    Line->setSpacing(6);
    Entry->Label = new QLabel(Text, Entry->Host);
    Entry->Label->setProperty("vx", "rowlabel");
    Entry->Label->setMinimumWidth(56);
    // (the order of Setup, which is not the order of the rows on the screen: the tests find the rows by it)
    Entry->Scan = new QPushButton(Entry->Host);
    Entry->Scan->setObjectName("VxScan");
    Entry->Scan->setProperty("vx", "binding");
    Entry->Scan->setProperty("row", (int)m_Rows.size());
    // one width for every binding: what "Press input…" and the longest names need
    Entry->Scan->setMinimumWidth(Entry->Scan->fontMetrics().horizontalAdvance(Tx("Pulsa una entrada…", "Press input…")) + 36);
    Entry->Scan->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    Entry->Scan->setCursor(Qt::PointingHandCursor);
    Entry->Scan->setFocusPolicy(Qt::StrongFocus);
    Entry->Scan->setAccessibleName(Text);
    Entry->Clear = new QToolButton(Entry->Host);
    Entry->Clear->setObjectName("VxClear");
    Entry->Clear->setProperty("vx", "clear");
    Entry->Clear->setText(QStringLiteral("×")); // (only while the pointer is on the row; the context menu has Clear too)
    Entry->Clear->setToolTip(Tx("Quitar la asignación", "Clear the assignment"));
    Entry->Clear->setAccessibleName(Tx("Borrar ", "Clear ") + Text);
    Entry->Clear->setCursor(Qt::PointingHandCursor);
    QSizePolicy Keep = Entry->Clear->sizePolicy();
    Keep.setRetainSizeWhenHidden(true);
    Entry->Clear->setSizePolicy(Keep);
    Line->addWidget(Entry->Label, 1);
    Line->addWidget(Entry->Scan);
    Line->addWidget(Entry->Clear);
    // while a row waits: it is brought into view and the buttons of the sequence follow
    Entry->OnCapturing = [this, Entry](bool On) {
        if (On && m_AssignScroll != nullptr)
        {
            m_AssignScroll->ensureWidgetVisible(Entry->Host);
        }
        UpdateAssignUi();
    };
    // right click on the capsule: assign or clear (while waiting, the right button is an input to assign)
    Entry->Scan->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(Entry->Scan, &QWidget::customContextMenuRequested, this, [this, Entry](const QPoint & Pos) {
        if (m_Scanner->Scanning())
        {
            return;
        }
        QMenu Menu;
        QAction * Assign = Menu.addAction(Tx("Asignar…", "Assign…"));
        QAction * Clear = Menu.addAction(Tx("Borrar", "Clear"));
        Clear->setEnabled(!IsEmptyButton(*Entry->Target));
        QAction * Chosen = Menu.exec(Entry->Scan->mapToGlobal(Pos));
        if (Chosen == Assign)
        {
            Entry->Scan->click();
        }
        else if (Chosen == Clear)
        {
            ClearRow(Entry);
        }
    });
    m_Rows.push_back(Entry);
    return Entry;
}

void ControllerPage::ClearRow(ScanRow * Row)
{
    if (m_Scanner->Scanning())
    {
        return;
    }
    *Row->Target = BUTTON();
    Row->Refresh();
    RefreshBoundDevice();
    SetModified();
}

void ControllerPage::SetHot(ScanRow * Row, bool On)
{
    Row->Hot = On;
    Row->Host->setProperty("hot", On);
    Row->Refresh();
}

void ControllerPage::Activate()
{
    m_Active = true;
    m_Scanner->SetPassThrough(m_CancelBtn);
    m_Pad->ClearPressed();
    m_Pad->SetEnabledLook(m_DeviceType->currentData().toInt() == PRESENT_CONT);
    if (IsTesting())
    {
        StartTest();
    }
}

void ControllerPage::Deactivate()
{
    if (m_Active)
    {
        StopTest();
    }
    m_Active = false;
    EndSequence();
    for (ScanRow * Row : m_Rows)
    {
        Row->Hot = false;
        Row->Host->setProperty("hot", false);
        Row->Refresh();
    }
}

// The row under the pointer shows its clear button; on the test page the keys and buttons of the application are followed
// to show which inputs are down
bool ControllerPage::eventFilter(QObject * Watched, QEvent * Event)
{
    if (Event->type() == QEvent::Enter || Event->type() == QEvent::Leave)
    {
        for (ScanRow * Row : m_Rows)
        {
            if (Row->Host == Watched)
            {
                SetHot(Row, Event->type() == QEvent::Enter);
            }
        }
    }
    if (m_Tabs->currentIndex() == 2)
    {
        switch (Event->type())
        {
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        {
            QKeyEvent * Key = static_cast<QKeyEvent *>(Event);
            if (!Key->isAutoRepeat())
            {
                const int Scancode = SdlScancodeFromEvent(Key);
                if (Scancode != 0)
                {
                    if (Event->type() == QEvent::KeyPress)
                    {
                        m_KeysDown.insert(Scancode);
                    }
                    else
                    {
                        m_KeysDown.erase(Scancode);
                    }
                }
            }
            return true; // (nothing else of the dialog may react to the keys while testing)
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        {
            QMouseEvent * Mouse = static_cast<QMouseEvent *>(Event);
            const int Number = SdlMouseButton(Mouse->button());
            if (Number >= 0 && Watched->isWidgetType() && static_cast<QWidget *>(Watched)->window() == window())
            {
                if (Event->type() == QEvent::MouseButtonPress)
                {
                    m_MouseDown.insert(Number);
                }
                else
                {
                    m_MouseDown.erase(Number);
                }
            }
            break;
        }
        case QEvent::Wheel:
        {
            const QWheelEvent * Wheel = static_cast<QWheelEvent *>(Event);
            const int Number = qAbs(Wheel->angleDelta().y()) >= qAbs(Wheel->angleDelta().x()) ? (Wheel->angleDelta().y() > 0 ? 32 : 33) : (Wheel->angleDelta().x() > 0 ? 34 : 35);
            m_MouseDown.insert(Number);
            QTimer::singleShot(150, this, [this, Number]() { m_MouseDown.erase(Number); });
            break;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(Watched, Event);
}

void ControllerPage::UpdateTest()
{
    const bool Controller = m_DeviceType->currentData().toInt() == PRESENT_CONT;
    m_Pad->SetEnabledLook(Controller);
    if (!Controller)
    {
        m_TestInfo->setText(Tx("Este control no es un mando N64: elige «Mando N64» en Dispositivo para probar las asignaciones.", "This control is not an N64 controller: choose “N64 Controller” as the device to test the assignments."));
        m_TestStick->clear();
        m_Pad->ClearPressed();
        return;
    }
    QStringList Down;
    bool First = true;
    for (size_t i = 0; i < m_Rows.size(); i++)
    {
        BUTTON & Button = *m_Rows[i]->Target;
        bool IsDown = false;
        if (Button.BtnType == BTNTYPE_KEYBUTTON)
        {
            IsDown = m_KeysDown.count(Button.Offset) != 0;
        }
        else if (Button.BtnType == BTNTYPE_MOUSEBUTTON)
        {
            IsDown = m_MouseDown.count(Button.Offset) != 0;
        }
        else if (!IsEmptyButton(Button))
        {
            IsDown = g_InputPlugin->IsButtonDown(Button, First);
            First = false;
        }
        m_Pad->SetPressed(m_Rows[i]->Control, IsDown);
        if (IsDown)
        {
            Down << m_Rows[i]->Label->text();
        }
    }
    auto Axis = [&](N64Control Plus, N64Control Minus) { return (m_Pad->StateOf(Plus) == N64ControlState::Pressed ? 1.0 : 0.0) - (m_Pad->StateOf(Minus) == N64ControlState::Pressed ? 1.0 : 0.0); };
    QPointF Stick(Axis(N64Control::StickRight, N64Control::StickLeft), Axis(N64Control::StickUp, N64Control::StickDown));
    if (Stick.x() != 0 && Stick.y() != 0)
    {
        Stick *= M_SQRT1_2;
    }
    m_Pad->SetStick(Stick);
    m_TestStick->setText(Tx("Palanca   X: %1      Y: %2", "Stick   X: %1      Y: %2").arg(Stick.x(), 0, 'f', 2).arg(Stick.y(), 0, 'f', 2));
    m_TestInfo->setText(Down.isEmpty() ? Tx("Pulsa botones para comprobar la configuración", "Press buttons to check the configuration") : Down.join("   ·   "));
}

void ControllerPage::ResetDefaults()
{
    g_InputPlugin->ResetController(m_Number, m_ControlInfo, m_Controller);
    DisplayController();
    SetModified();
}

// Scan the next row of the sequence (OnScanSuccess), in the order of the cards
void ControllerPage::SetupNext()
{
    if (m_SetupIndex < 0 || m_SetupIndex >= (int)m_Sequence.size())
    {
        EndSequence();
        return;
    }
    ScanRow * Row = m_Rows[m_Sequence[m_SetupIndex]];
    m_SequenceStatus->setText(Tx("Asignando %1 de %2: %3", "Assigning %1 of %2: %3").arg(m_SetupIndex + 1).arg(m_Sequence.size()).arg(Row->Label->text()));
    m_Scanner->Start(
        Row, [this](const BUTTON & Button) { m_Owner->RemoveMapping(Button); RefreshBoundDevice(); SetModified(); },
        [this](bool Success) {
            if (!Success)
            {
                EndSequence(); // OnScanCanceled
                RefreshBoundDevice();
                return;
            }
            m_SetupIndex += 1;
            RefreshBoundDevice();
            SetupNext();
        });
}

void ControllerPage::EndSequence()
{
    m_SetupIndex = -1;
    UpdateAssignUi();
}

// The cancel button and the progress of the sequence show while an input is awaited or a sequence runs
void ControllerPage::UpdateAssignUi()
{
    const bool Busy = m_Scanner->Scanning() || m_SetupIndex >= 0;
    m_CancelBtn->setVisible(Busy);
    m_SequenceStatus->setVisible(m_SetupIndex >= 0);
    m_SetupBtn->setEnabled(!Busy && m_DeviceType->currentData().toInt() == PRESENT_CONT);
}

void ControllerPage::SetModified()
{
    Modified = true;
}

void ControllerPage::RefreshBoundDevice()
{
    const QString Devices = FromWide(g_InputPlugin->ControllerDevices(m_Controller));
    m_BoundDevice->setText(Devices.isEmpty() ? QString() : Devices);
    m_BoundDevice->setToolTip(Devices.isEmpty() ? QString() : Tx("Dispositivos asignados: ", "Bound devices: ") + Devices);
}

void ControllerPage::RefreshStatus()
{
    const int Present = m_DeviceType->currentData().toInt();
    m_Status->setText(Present == PRESENT_NONE ? Tx("No conectado", "Not connected") : Tx("Conectado", "Connected"));
    m_Status->setEnabled(Present != PRESENT_NONE);
}

void ControllerPage::RefreshRows()
{
    for (ScanRow * Row : m_Rows)
    {
        Row->Refresh();
    }
}

void ControllerPage::DisplayController()
{
    int Index = m_DeviceType->findData((int)m_ControlInfo.Present);
    m_DeviceType->blockSignals(true);
    m_DeviceType->setCurrentIndex(Index >= 0 ? Index : 0);
    m_DeviceType->blockSignals(false);
    m_Range->blockSignals(true);
    m_DeadZone->blockSignals(true);
    m_Sensitivity->blockSignals(true);
    m_Range->setValue(m_Controller.Range);
    m_DeadZone->setValue(m_Controller.DeadZone);
    m_Sensitivity->setValue(m_Controller.Sensitivity > 0 ? m_Controller.Sensitivity : 1);
    m_Range->blockSignals(false);
    m_DeadZone->blockSignals(false);
    m_Sensitivity->blockSignals(false);
    m_RangeLabel->setText(QString("%1%").arg(m_Range->value()));
    m_DeadZoneLabel->setText(QString("%1%").arg(m_DeadZone->value()));
    m_SensitivityLabel->setText(QString("%1%").arg(m_Sensitivity->value()));
    m_RealRange->setChecked(m_Controller.RealN64Range);
    m_RemoveDuplicate->setChecked(m_Controller.RemoveDuplicate);
    RefreshRows();
    RefreshBoundDevice();
    RefreshStatus();
}

void ControllerPage::EnablePage(int32_t Present)
{
    bool Enable = Present == PRESENT_CONT;
    for (QWidget * Widget : m_ControllerOnly)
    {
        Widget->setEnabled(Enable);
    }
    m_Pad->SetEnabledLook(Enable);
    RefreshStatus();
    UpdateAssignUi();
    Enable = Present != PRESENT_NONE;
    m_DefaultsBtn->setEnabled(Enable);
    m_RealRange->setEnabled(Enable);
    m_RemoveDuplicate->setEnabled(Enable);
    m_Sensitivity->setEnabled(Present == PRESENT_MOUSE);
}

// OnApply
void ControllerPage::Apply()
{
    N64CONTROLLER & Controller = g_InputPlugin->Controllers(m_Number);
    if (InputDebug())
    {
        fprintf(stderr, "[input] Apply control %u: present=%d B type=%d offset=%u\n", m_Number, m_DeviceType->currentData().toInt(), (int)m_Controller.B_BUTTON.BtnType, (unsigned)m_Controller.B_BUTTON.Offset);
    }
    Controller = m_Controller;
    Controller.Range = (uint8_t)m_Range->value();
    Controller.DeadZone = (uint8_t)m_DeadZone->value();
    Controller.Sensitivity = (uint8_t)m_Sensitivity->value();
    Controller.RealN64Range = m_RealRange->isChecked();
    Controller.RemoveDuplicate = m_RemoveDuplicate->isChecked();
    CONTROL & ControlInfo = g_InputPlugin->ControlInfo(m_Number);
    ControlInfo.Present = m_DeviceType->currentData().toInt();
    ControlInfo.Plugin = m_ControlInfo.Plugin;
    g_InputPlugin->SaveController(m_Number);
}

// CControllerSettings::RemoveMapping: a button can only drive one control when
// "Remove duplicate mappings" is on
void ControllerPage::RemoveMapping(const BUTTON & Button)
{
    if (!m_Controller.RemoveDuplicate || IsEmptyButton(Button))
    {
        return;
    }
    std::vector<ScanRow *> Moved;
    for (ScanRow * Row : m_Rows)
    {
        if (SameButton(*Row->Target, Button))
        {
            *Row->Target = BUTTON();
            Moved.push_back(Row);
        }
    }
    if (!Moved.empty())
    {
        // (a page that is not the one being edited has to be saved too)
        SetModified();
        RefreshBoundDevice();
        RefreshRows();
        for (ScanRow * Row : Moved)
        {
            Row->Scan->setToolTip(Tx("La asignación se movió al control que acabas de asignar", "The mapping was moved to the control that was just assigned"));
            if (m_Active)
            {
                QToolTip::showText(Row->Scan->mapToGlobal(QPoint(0, Row->Scan->height())), Row->Scan->toolTip(), Row->Scan);
            }
        }
    }
}
} // namespace

extern "C" __attribute__((visibility("default"))) void PJ64InputQtConfig(void * /*hParent*/)
{
    int Argc = 0;
    QApplication * Owned = nullptr;
    if (QApplication::instance() == nullptr)
    {
        static char Name[] = "vortex64";
        static char * Argv[] = {Name, nullptr};
        Argc = 1;
        Owned = new QApplication(Argc, Argv);
    }
    if (g_InputPlugin != nullptr)
    {
        g_InputPlugin->NotifyConfigDialogOpen(true);
    }
    {
        InputDialog Dialog(QApplication::activeWindow());
        Dialog.exec();
    }
    if (g_InputPlugin != nullptr)
    {
        g_InputPlugin->NotifyConfigDialogOpen(false);
    }
    delete Owned;
}
