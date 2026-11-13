#include "ScreenSaverInhibit.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

ScreenSaverInhibitor::ScreenSaverInhibitor(QObject * Parent) :
    QObject(Parent)
{
}

ScreenSaverInhibitor::~ScreenSaverInhibitor()
{
    if (m_HaveCookie)
    {
        Release();
    }
}

void ScreenSaverInhibitor::Set(bool Inhibit)
{
    m_Wanted = Inhibit;
    if (Inhibit)
    {
        if (!m_HaveCookie && !m_Requesting)
        {
            Request();
        }
    }
    else if (m_HaveCookie)
    {
        Release();
    }
}

void ScreenSaverInhibitor::Request()
{
    QDBusConnection Bus = QDBusConnection::sessionBus();
    if (!Bus.isConnected() || Bus.interface() == nullptr)
    {
        return;
    }
    QDBusMessage Message;
    if (Bus.interface()->isServiceRegistered("org.freedesktop.ScreenSaver"))
    {
        m_Gnome = false;
        m_ServiceName = "ScreenSaver";
        Message = QDBusMessage::createMethodCall("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver", "Inhibit");
        Message << QString("Vortex64") << QString("Emulating a game");
    }
    else if (Bus.interface()->isServiceRegistered("org.gnome.SessionManager"))
    {
        m_Gnome = true;
        m_ServiceName = "GnomeSessionManager";
        // flags: 8 = inhibit the session being marked idle
        Message = QDBusMessage::createMethodCall("org.gnome.SessionManager", "/org/gnome/SessionManager", "org.gnome.SessionManager", "Inhibit");
        Message << QString("vortex64") << uint(0) << QString("Emulating a game") << uint(8);
    }
    else
    {
        return;
    }
    m_Requesting = true;
    QDBusPendingCallWatcher * Watcher = new QDBusPendingCallWatcher(Bus.asyncCall(Message, 5000), this);
    connect(Watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher * Call) {
        QDBusPendingReply<uint> Reply = *Call;
        m_Requesting = false;
        if (Reply.isValid())
        {
            m_Cookie = Reply.value();
            m_HaveCookie = true;
            // the game paused or ended while the request was on its way
            if (!m_Wanted)
            {
                Release();
            }
        }
        Call->deleteLater();
    });
}

void ScreenSaverInhibitor::Release()
{
    QDBusConnection Bus = QDBusConnection::sessionBus();
    if (Bus.isConnected())
    {
        QDBusMessage Message = m_Gnome ? QDBusMessage::createMethodCall("org.gnome.SessionManager", "/org/gnome/SessionManager", "org.gnome.SessionManager", "Uninhibit")
                                       : QDBusMessage::createMethodCall("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver", "UnInhibit");
        Message << m_Cookie;
        Bus.call(Message, QDBus::NoBlock);
    }
    m_HaveCookie = false;
    m_Cookie = 0;
}
