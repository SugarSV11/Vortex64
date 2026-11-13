#include "QtNotification.h"
#include <Vortex64-core/Settings/SettingsID.h>
#include <Vortex64-ui/Settings/UISettings.h>
#include "MainWindow.h"

#include <Common/StdString.h>
#include <Vortex64-core/Multilanguage/Language.h>
#include <Vortex64-core/N64System/N64System.h>
#include <Vortex64-core/N64System/SystemGlobals.h>
#include <Vortex64-core/Plugins/GFXPlugin.h>
#include <Vortex64-core/Plugins/Plugin.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaObject>
#include <QThread>
#include <pthread.h>

QtNotification::QtNotification(MainWindow * mainWindow) :
    QObject(mainWindow),
    m_MainWindow(mainWindow)
{
}

Qt::ConnectionType QtNotification::InvokeConnectionType() const
{
    return QThread::currentThread() == this->thread() ? Qt::DirectConnection : Qt::QueuedConnection;
}

void QtNotification::doDisplayError(const QString & Message)
{
    m_MainWindow->ShowErrorDialog(tr("Error"), Message);
}

void QtNotification::doDisplayWarning(const QString & Message)
{
    m_MainWindow->ShowWarningDialog(Message);
}

void QtNotification::doDisplayMessage(const QString & Message)
{
    m_MainWindow->ShowStatusMessage(Message, 5000);
}

bool QtNotification::doAskYesNoQuestion(const QString & Question)
{
    return m_MainWindow->AskYesNo(Question);
}

void QtNotification::doChangeFullScreen()
{
    m_MainWindow->SetFullScreenRequested();
}

void QtNotification::DisplayError(const char * Message) const
{
    const QString msg = QString::fromUtf8(Message ? Message : "");
    QMetaObject::invokeMethod(const_cast<QtNotification *>(this), [this, msg]()
                               { const_cast<QtNotification *>(this)->doDisplayError(msg); },
                               InvokeConnectionType());
}

void QtNotification::DisplayError(LanguageStringID StringID) const
{
    DisplayError(GS(StringID));
}

// CNotificationImp::FatalError: the message is shown (the caller waits for it to be closed) and the
// calling thread, which is the emulation thread when the core asks, ends: what it was doing cannot
// go on (memory that could not be allocated, code that could not be read)
void QtNotification::FatalError(const char * Message) const
{
    const QString msg = QString::fromUtf8(Message ? Message : "");
    const bool InGuiThread = QThread::currentThread() == this->thread();
    QMetaObject::invokeMethod(const_cast<QtNotification *>(this), [this, msg]()
                               { const_cast<QtNotification *>(this)->doDisplayError(msg); },
                               InGuiThread ? Qt::DirectConnection : Qt::BlockingQueuedConnection);
    if (!InGuiThread)
    {
        pthread_exit(nullptr);
    }
}

void QtNotification::FatalError(LanguageStringID StringID) const
{
    FatalError(GS(StringID));
}

void QtNotification::DisplayWarning(const char * Message) const
{
    const QString msg = QString::fromUtf8(Message ? Message : "");
    QMetaObject::invokeMethod(const_cast<QtNotification *>(this), [this, msg]()
                               { const_cast<QtNotification *>(this)->doDisplayWarning(msg); },
                               InvokeConnectionType());
}

void QtNotification::DisplayWarning(LanguageStringID StringID) const
{
    DisplayWarning(GS(StringID));
}

// CNotificationImp::DisplayMessage: the time rule of upstream, and in the full screen the message
// is drawn by the video plugin (there is no status bar)
void QtNotification::DisplayMessage(int DisplayTime, const char * Message) const
{
    {
        std::lock_guard<std::mutex> Guard(m_MessageLock);
        if (m_NextMsg > 0 || DisplayTime > 0)
        {
            const time_t Now = time(nullptr);
            if (DisplayTime == 0 && Now < m_NextMsg)
            {
                return;
            }
            if (DisplayTime > 0)
            {
                m_NextMsg = Now + DisplayTime;
            }
        }
    }
    if (UISettingsLoadBool(UserInterface_InFullScreen))
    {
        if (g_Plugins != nullptr && g_Plugins->Gfx() != nullptr && g_Plugins->Gfx()->DrawStatus != nullptr)
        {
            g_Plugins->Gfx()->DrawStatus(Message ? Message : "", 0);
        }
        return;
    }
    const QString msg = QString::fromUtf8(Message ? Message : "");
    QMetaObject::invokeMethod(const_cast<QtNotification *>(this), [this, msg]()
                               { const_cast<QtNotification *>(this)->doDisplayMessage(msg); },
                               InvokeConnectionType());
}

void QtNotification::DisplayMessage(int DisplayTime, LanguageStringID StringID) const
{
    DisplayMessage(DisplayTime, GS(StringID));
}

// the second pane of the status bar
void QtNotification::DisplayMessage2(const char * Message) const
{
    const QString msg = QString::fromUtf8(Message ? Message : "");
    QMetaObject::invokeMethod(const_cast<QtNotification *>(this), [this, msg]()
                               { m_MainWindow->ShowStatusMessage2(msg); },
                               InvokeConnectionType());
}

bool QtNotification::AskYesNoQuestion(const char * Question) const
{
    const QString question = QString::fromUtf8(Question ? Question : "");
    bool result = false;
    const Qt::ConnectionType connectionType = QThread::currentThread() == this->thread() ? Qt::DirectConnection : Qt::BlockingQueuedConnection;
    QMetaObject::invokeMethod(const_cast<QtNotification *>(this), [this, question, &result]()
                               { result = const_cast<QtNotification *>(this)->doAskYesNoQuestion(question); },
                               connectionType);
    return result;
}

void QtNotification::BreakPoint(const char * FileName, int32_t LineNumber)
{
    // Mirrors Vortex64-cli/LinuxNotification.cpp: no native debugger UI
    // here either, so always take the "stop cleanly" path instead of
    // silently continuing past something the core flagged as unexpected
    // or unimplemented (see that file's comment for why this matters -
    // an incomplete JIT opcode is exactly the kind of thing this catches).
    DisplayError(stdstr_f("Break point found at\n%s\n%d\n\nStopping emulation", FileName ? FileName : "", LineNumber).c_str());
    if (g_BaseSystem)
    {
        g_BaseSystem->CloseCpu();
    }
}

void QtNotification::AppInitDone(void)
{
    // Same as CNotificationImp::AppInitDone in Project64/UserInterface/
    // Notification.cpp: the frontend registers its own settings once the core
    // has created g_Settings.
    RegisterUISettings();
}

bool QtNotification::ProcessGuiMessages(void) const
{
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    return false;
}

void QtNotification::ChangeFullScreen(void) const
{
    QMetaObject::invokeMethod(const_cast<QtNotification *>(this), [this]()
                               { const_cast<QtNotification *>(this)->doChangeFullScreen(); },
                               InvokeConnectionType());
}
