#pragma once

#include <QLineEdit>
#include <QThread>
#include <QCoreApplication>
#include <functional>
#include <stdint.h>

// The debugger windows live in the GUI thread, while the core reports events from the
// emulation thread (breakpoints, stepping, settings callbacks). Windows have one thread
// per window; here every window operation is marshalled to the GUI thread.
inline void RunOnGuiThread(std::function<void()> Function)
{
    QCoreApplication * App = QCoreApplication::instance();
    if (App == nullptr)
    {
        return;
    }
    if (QThread::currentThread() == App->thread())
    {
        Function();
    }
    else
    {
        QMetaObject::invokeMethod(App, std::move(Function), Qt::QueuedConnection);
    }
}

// CEditNumber32 counterpart: a line edit for a 32 bit number shown in hex (upper case,
// zero extended) or decimal.
class NumberEdit : public QLineEdit
{
    Q_OBJECT
public:
    enum Display
    {
        DisplayHex,
        DisplayDec
    };

    explicit NumberEdit(QWidget * Parent = nullptr, Display Mode = DisplayHex, int MaxDigits = 8);

    uint32_t Value() const;
    // ZeroExtend: pad hex values to MaxDigits
    void SetValue(uint32_t Value, bool ZeroExtend = true);
    bool HasText() const
    {
        return !text().isEmpty();
    }

Q_SIGNALS:
    // The user edited the text (not SetValue)
    void ValueEdited();

private:
    Display m_Display;
    int m_MaxDigits;
};
