#pragma once

#include <QObject>
#include <QString>

// Linux counterpart of the SC_SCREENSAVE / SC_MONITORPOWER handling of CMainGui::MainGui_Proc:
// while a game runs (not paused) and Setting_DisableScrSaver is on, the screen saver and the
// monitor power saving must not start. The desktop is asked to inhibit its idle handling over
// D-Bus (org.freedesktop.ScreenSaver of KDE, XFCE and others; org.gnome.SessionManager of GNOME)
// and released when the game pauses or ends.
class ScreenSaverInhibitor : public QObject
{
    Q_OBJECT
public:
    explicit ScreenSaverInhibitor(QObject * Parent = nullptr);
    ~ScreenSaverInhibitor() override;

    // Whether the desktop should be kept awake; the request to the desktop is asynchronous
    void Set(bool Inhibit);
    bool Wanted() const
    {
        return m_Wanted;
    }
    // The desktop granted the inhibition (Cookie is what UnInhibit needs)
    bool Inhibited() const
    {
        return m_HaveCookie;
    }
    // "ScreenSaver", "GnomeSessionManager" or empty when no desktop service was found
    QString Service() const
    {
        return m_ServiceName;
    }

private:
    void Request();
    void Release();

    bool m_Wanted = false;
    bool m_Requesting = false;
    bool m_HaveCookie = false;
    uint m_Cookie = 0;
    QString m_ServiceName;
    bool m_Gnome = false;
};
