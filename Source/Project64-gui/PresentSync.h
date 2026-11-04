// The synchronization of the presentation with the monitor, on the side of the frontend.
//
// It finds the monitor that holds most of the window of the emulation, reads its refresh rate (Qt: QScreen), and tells the video
// plugin - with the mode the user chose (Auto, On, Off), the frame rate of the game and whether the emulation is unthrottled - so
// that the plugin sets the swap interval of its context (Project64-plugin-spec/PresentControl.h). The refresh rate is NEVER the speed
// of the emulation: the speed stays the one of the speed limiter of the core, whatever the monitor.
//
// What changes it: the window moves to another monitor, the monitor changes its mode, a monitor is added or removed, the setting
// changes, a game starts, the speed changes. It does not touch the renderer: the plugin applies the new interval at its next swap.
//
// VORTEX_PRESENT_TRACE=1 prints the monitor, the hint sent and, every five seconds, what the plugin applied and the VI/s measured.
// VORTEX_FAKE_REFRESH=<Hz> replaces the refresh rate read from Qt (the tests, a monitor that is not at hand).
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QWindow>
#include <Project64-plugin-spec/PresentControl.h>

class QScreen;
class QWidget;

namespace Vortex
{
struct DisplayInfo
{
    QString Name;
    QSize Size;           // in pixels of the screen (device pixels)
    double RefreshHz = 0; // 0: not known
    bool Fake = false;    // VORTEX_FAKE_REFRESH
    QString Text() const; // "1920×1080 @ 100 Hz" (or without "@" when the rate is not known)
};

class PresentSync : public QObject
{
    Q_OBJECT
public:
    explicit PresentSync(QWidget * Window);
    ~PresentSync() override;
    // the one of the running application (null in a program without a main window)
    static PresentSync * Instance();

    // a rate that Qt reports and that can be believed: 20..1000 Hz, otherwise 0 (not known)
    static double SaneRefresh(double Hz);
    // the screen that holds most of a rectangle (the intersection with the geometry of each screen), Fallback when it is on none
    static QScreen * ScreenHolding(const QRect & Frame, const QList<QScreen *> & Screens, QScreen * Fallback);
    static DisplayInfo Describe(QScreen * Screen);
    // the monitor of a window now (the same rule the instance applies)
    static DisplayInfo DisplayOf(QWidget * Window);

    DisplayInfo Display() const
    {
        return m_Display;
    }
    int Mode() const;
    // the maximum FPS of the user: 0 (automatic) or MinMaxFps..MaxMaxFps; anything else in the file of settings is automatic
    static constexpr int MinMaxFps = 10, MaxMaxFps = 1000;
    static int ValidMaxFps(int64_t Value)
    {
        return Value >= MinMaxFps && Value <= MaxMaxFps ? (int)Value : 0;
    }
    int MaxFps() const;
    // The hint for the plugin as it is now (what Refresh() sends)
    PJ64_PRESENT_HINT Hint() const;
    // Looks again at everything and sends the hint if it changed (or the plugin is another one)
    void Refresh();
    int HintsSent() const
    {
        return m_HintsSent;
    }
    int DisplayChanges() const
    {
        return m_DisplayChanges;
    }

Q_SIGNALS:
    void DisplayChanged();

private:
    void Hook();
    void TraceStatus();

    QWidget * m_Window;
    QTimer m_Timer;
    DisplayInfo m_Display;
    QPointer<QScreen> m_Screen;
    QPointer<QWindow> m_Hooked;
    PJ64_PRESENT_HINT m_Sent = {-1, -1, -1, -1, -1};
    int m_HintsSent = 0;
    int m_DisplayChanges = 0;
    int m_Ticks = 0;
    QElapsedTimer m_ViClock;
    quint64 m_ViLast = 0;
    qint64 m_PresentedLast = 0, m_SkippedLast = 0;
};
} // namespace Vortex
