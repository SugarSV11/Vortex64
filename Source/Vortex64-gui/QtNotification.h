#pragma once

#include <Vortex64-core/Notification.h>

#include <QObject>
#include <QString>
#include <ctime>
#include <mutex>

// CNotification implementation for the Qt GUI. CN64System runs the
// emulation on its own worker thread (see StartEmulationThread() in
// N64System.cpp) and calls into g_Notify from there, but every Qt widget
// (QMessageBox, the status bar, ...) may only be touched from the GUI
// thread. Every DisplayX()/AskYesNoQuestion() call below is a const
// CNotification method that can be reached from either thread, so each
// one marshals to doDisplayX()/doAskYesNoQuestion() slots that live on
// this QObject (which itself lives on the GUI thread) via
// QMetaObject::invokeMethod - Direct if we're already on the GUI thread
// (invokeMethod's own BlockingQueuedConnection would deadlock in that
// case), BlockingQueuedConnection otherwise so AskYesNoQuestion can still
// return a real answer to a worker thread that is genuinely blocked on it.
class MainWindow;

class QtNotification : public QObject, public CNotification
{
    Q_OBJECT
public:
    explicit QtNotification(MainWindow * mainWindow);

    void DisplayError(const char * Message) const override;
    void DisplayError(LanguageStringID StringID) const override;

    void FatalError(const char * Message) const override;
    void FatalError(LanguageStringID StringID) const override;

    void DisplayWarning(const char * Message) const override;
    void DisplayWarning(LanguageStringID StringID) const override;

    void DisplayMessage(int DisplayTime, const char * Message) const override;
    void DisplayMessage(int DisplayTime, LanguageStringID StringID) const override;
    void DisplayMessage2(const char * Message) const override;

    bool AskYesNoQuestion(const char * Question) const override;

    void BreakPoint(const char * FileName, int32_t LineNumber) override;

    void AppInitDone(void) override;
    bool ProcessGuiMessages(void) const override;
    void ChangeFullScreen(void) const override;

private:
    // Plain private methods, not slots: called via the functor overload of
    // QMetaObject::invokeMethod() below, which only needs `this` to be a
    // QObject for thread affinity - it doesn't require these to be
    // registered as slots.
    void doDisplayError(const QString & Message);
    void doDisplayWarning(const QString & Message);
    void doDisplayMessage(const QString & Message);
    bool doAskYesNoQuestion(const QString & Question);
    void doChangeFullScreen();

    Qt::ConnectionType InvokeConnectionType() const;

    MainWindow * m_MainWindow;
    // CNotificationImp::m_NextMsg: a message with a display time keeps the status bar that long
    // against the messages without one (emulation speed and the like)
    mutable std::mutex m_MessageLock;
    mutable time_t m_NextMsg = 0;
};
