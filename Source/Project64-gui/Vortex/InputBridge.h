#pragma once

#include <QObject>
#include <Qt>
#include <QPoint>
#include <QSet>

class QWidget;

namespace Vortex
{
// The keyboard and the mouse of a game that runs in the window of Vortex64. The picture is drawn by a video plugin in a window
// that SDL embeds in the one of Qt, so SDL never has the focus of the keyboard: its own state of keys and buttons does not
// change and the input plugin would see nothing. Qt receives the events (that is what the focus of the application is), and this
// gives them to the plugin (VortexInputEvent of libProject64-input): the keys by their evdev code, the buttons of the mouse by
// number, the wheel as pulses and the movement of the pointer. Only while the window is the active one and a game runs; when
// the window loses the activation everything is released.
class InputBridge : public QObject
{
    Q_OBJECT
public:
    explicit InputBridge(QWidget * Window);
    ~InputBridge() override;
    bool eventFilter(QObject * Watched, QEvent * Event) override;
    // For the tests: the call that gives an event to the plugin goes through this too
    static int ButtonNumber(int QtButton); // SDL number - 1 (left 0, middle 1, right 2, back 3, forward 4, ...); -1: none

private:
    void Send(int Kind, int Code, int Value);
    void ReleaseAll();
    bool Active() const;
    bool KeyIsAssigned(int Code) const;
    bool HasRealModifier(Qt::KeyboardModifiers Held) const;

    QWidget * m_Window;
    QSet<int> m_Keys;
    QSet<int> m_Buttons;
    QPoint m_LastPointer;
    bool m_HaveLast = false;
    void (*m_Function)(int, int, int) = nullptr;
};
} // namespace Vortex
