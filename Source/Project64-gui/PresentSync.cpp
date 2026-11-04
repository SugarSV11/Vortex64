#include "PresentSync.h"

#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>
#include <Project64-core/Plugins/GFXPlugin.h>
#include <Project64-core/Plugins/Plugin.h>
#include <Project64-core/Settings.h>

#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace Vortex
{
namespace
{
PresentSync * s_Instance = nullptr;

bool Trace()
{
    static const bool On = qEnvironmentVariable("VORTEX_PRESENT_TRACE") == QLatin1String("1");
    return On;
}

const char * ModeName(int Mode)
{
    return Mode == PJ64_VSYNC_ON ? "on" : (Mode == PJ64_VSYNC_OFF ? "off" : "auto");
}
} // namespace

QString DisplayInfo::Text() const
{
    const QString Resolution = QString::fromUtf8("%1×%2").arg(Size.width()).arg(Size.height());
    if (RefreshHz <= 0)
    {
        return Resolution;
    }
    // 59.94 Hz shows as it is, 100 Hz as 100
    const double Rounded = std::round(RefreshHz);
    const QString Rate = std::abs(RefreshHz - Rounded) < 0.05 ? QString::number((int)Rounded) : QString::number(RefreshHz, 'f', 2);
    return Resolution + QStringLiteral(" @ ") + Rate + QStringLiteral(" Hz");
}

PresentSync * PresentSync::Instance()
{
    return s_Instance;
}

double PresentSync::SaneRefresh(double Hz)
{
    return (Hz >= 20.0 && Hz <= 1000.0) ? Hz : 0.0;
}

QScreen * PresentSync::ScreenHolding(const QRect & Frame, const QList<QScreen *> & Screens, QScreen * Fallback)
{
    QScreen * Best = nullptr;
    qint64 BestArea = 0;
    for (QScreen * Screen : Screens)
    {
        const QRect Common = Frame.intersected(Screen->geometry());
        const qint64 Area = Common.isEmpty() ? 0 : (qint64)Common.width() * Common.height();
        if (Area > BestArea)
        {
            BestArea = Area;
            Best = Screen;
        }
    }
    return Best != nullptr ? Best : Fallback;
}

DisplayInfo PresentSync::Describe(QScreen * Screen)
{
    DisplayInfo Info;
    if (Screen != nullptr)
    {
        Info.Name = Screen->name();
        Info.Size = Screen->size() * Screen->devicePixelRatio();
        Info.RefreshHz = SaneRefresh(Screen->refreshRate());
    }
    bool Ok = false;
    const double Fake = qEnvironmentVariable("VORTEX_FAKE_REFRESH").toDouble(&Ok);
    if (Ok && Fake > 0)
    {
        Info.RefreshHz = Fake;
        Info.Fake = true;
    }
    return Info;
}

DisplayInfo PresentSync::DisplayOf(QWidget * Window)
{
    QScreen * Fallback = (Window != nullptr && Window->windowHandle() != nullptr) ? Window->windowHandle()->screen() : QGuiApplication::primaryScreen();
    QScreen * Screen = Fallback;
    if (Window != nullptr && Window->isVisible())
    {
        Screen = ScreenHolding(Window->frameGeometry(), QGuiApplication::screens(), Fallback);
    }
    return Describe(Screen);
}

PresentSync::PresentSync(QWidget * Window) :
    QObject(Window),
    m_Window(Window)
{
    s_Instance = this;
    // the screens of the application: a monitor that comes or goes, or changes its mode
    connect(qApp, &QGuiApplication::screenAdded, this, [this]() { Refresh(); });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this]() { Refresh(); });
    connect(qApp, &QGuiApplication::primaryScreenChanged, this, [this]() { Refresh(); });
    // (the rest - the window that moves between monitors by a pixel at a time, the speed that changes, a game that starts - is seen
    // by the timer; a tick only compares and sends nothing when nothing changed)
    m_Timer.setInterval(250);
    connect(&m_Timer, &QTimer::timeout, this, [this]() {
        Refresh();
        if (Trace() && ++m_Ticks % 20 == 0)
        {
            TraceStatus();
        }
    });
    m_Timer.start();
    Refresh();
}

PresentSync::~PresentSync()
{
    if (s_Instance == this)
    {
        s_Instance = nullptr;
    }
}

int PresentSync::Mode() const
{
    const uint32_t Value = g_Settings != nullptr ? g_Settings->LoadDword(UserInterface_VSyncMode) : 0;
    return Value == PJ64_VSYNC_ON ? PJ64_VSYNC_ON : (Value == PJ64_VSYNC_OFF ? PJ64_VSYNC_OFF : PJ64_VSYNC_AUTO);
}

int PresentSync::MaxFps() const
{
    return g_Settings != nullptr ? ValidMaxFps(g_Settings->LoadDword(UserInterface_MaxFps)) : 0;
}

PJ64_PRESENT_HINT PresentSync::Hint() const
{
    PJ64_PRESENT_HINT Hint = {};
    Hint.Mode = Mode();
    Hint.RefreshMilliHz = (int32_t)std::lround(m_Display.RefreshHz * 1000.0);
    const uint32_t GameHz = g_Settings != nullptr ? g_Settings->LoadDword(GameRunning_ScreenHertz) : 0;
    Hint.GameMilliHz = (int32_t)(GameHz * 1000);
    Hint.Unthrottled = (g_BaseSystem != nullptr && g_BaseSystem->RunsUnthrottled()) ? 1 : 0;
    Hint.MaxMilliFps = MaxFps() * 1000;
    return Hint;
}

void PresentSync::Hook()
{
    // the window of the screen: it says at once when it changes monitor (the timer would say it within a quarter of a second)
    QWindow * Handle = m_Window != nullptr ? m_Window->windowHandle() : nullptr;
    if (Handle != nullptr && Handle != m_Hooked)
    {
        m_Hooked = Handle;
        connect(Handle, &QWindow::screenChanged, this, [this]() { Refresh(); });
    }
}

void PresentSync::Refresh()
{
    Hook();
    const DisplayInfo Now = DisplayOf(m_Window);
    QScreen * Screen = nullptr;
    for (QScreen * S : QGuiApplication::screens())
    {
        if (S->name() == Now.Name)
        {
            Screen = S;
        }
    }
    if (Screen != m_Screen)
    {
        if (m_Screen != nullptr)
        {
            disconnect(m_Screen, nullptr, this, nullptr);
        }
        m_Screen = Screen;
        if (m_Screen != nullptr)
        {
            connect(m_Screen, &QScreen::refreshRateChanged, this, [this]() { Refresh(); });
            connect(m_Screen, &QScreen::geometryChanged, this, [this]() { Refresh(); });
        }
    }
    const bool Changed = Now.Name != m_Display.Name || Now.Size != m_Display.Size || std::abs(Now.RefreshHz - m_Display.RefreshHz) > 0.001;
    if (Changed)
    {
        m_Display = Now;
        m_DisplayChanges++;
        if (Trace())
        {
            fprintf(stderr, "[present] display: %s %s%s\n", Now.Name.toUtf8().constData(), Now.Text().toUtf8().constData(), Now.Fake ? " (VORTEX_FAKE_REFRESH)" : (Now.RefreshHz <= 0 ? " (the refresh rate is not known)" : ""));
        }
        Q_EMIT DisplayChanged();
    }

    // the plugin: the hint goes to the plugins object when it differs from the last one sent (that object keeps it for every video plugin
    // that is loaded now or later, and does not call a plugin that is being deleted)
    if (g_Plugins == nullptr)
    {
        return;
    }
    const PJ64_PRESENT_HINT Hint = this->Hint();
    if (m_Sent.Mode != -1 && Hint.Mode == m_Sent.Mode && Hint.RefreshMilliHz == m_Sent.RefreshMilliHz && Hint.GameMilliHz == m_Sent.GameMilliHz && Hint.Unthrottled == m_Sent.Unthrottled && Hint.MaxMilliFps == m_Sent.MaxMilliFps)
    {
        return;
    }
    // the settings dialogs of the plugins know that the frontend decides (their own "vertical sync" box is not the one that counts)
    qputenv("VORTEX_VSYNC_MANAGED", "1");
    const bool Delivered = g_Plugins->SetPresentHint(Hint);
    m_Sent = Hint;
    m_HintsSent++;
    if (Trace())
    {
        PJ64_PRESENT_STATUS Ignored = {};
        std::string Name;
        g_Plugins->GetPresentStatus(Ignored, &Name);
        fprintf(stderr, "[present] hint %s %s: mode %s, monitor %.3f Hz, game %.3f Hz, %s, maximum FPS %s\n", Delivered ? "sent to" : "kept for the next video plugin, now", Name.empty() ? "none" : Name.c_str(), ModeName(Hint.Mode),
            Hint.RefreshMilliHz / 1000.0, Hint.GameMilliHz / 1000.0, Hint.Unthrottled != 0 ? "fast forward / unthrottled (the limit is not applied)" : "throttled", Hint.MaxMilliFps == 0 ? "auto" : QByteArray::number(Hint.MaxMilliFps / 1000).constData());
    }
}

void PresentSync::TraceStatus()
{
    PJ64_PRESENT_STATUS Status = {};
    std::string Name;
    if (g_Plugins == nullptr || g_BaseSystem == nullptr || !g_Plugins->GetPresentStatus(Status, &Name))
    {
        return;
    }
    // the VI/s of the emulator: the difference of the VI counter over the wall clock time since the last line
    const uint64_t Count = g_BaseSystem->ViCount();
    const qint64 Now = m_ViClock.isValid() ? m_ViClock.restart() : (m_ViClock.start(), 0);
    const double Rate = (Now > 0 && Count >= m_ViLast) ? (Count - m_ViLast) * 1000.0 / Now : 0.0;
    m_ViLast = Count;
    const int64_t Shown = Status.Presented - m_PresentedLast;
    const int64_t Dropped = Status.Skipped - m_SkippedLast;
    m_PresentedLast = Status.Presented;
    m_SkippedLast = Status.Skipped;
    fprintf(stderr, "[present] %s: VSync %s, maximum FPS %s, swap interval requested %d, active %d, adaptive %s, monitor %.3f Hz, emulator %.1f VI/s (game %.0f), presented %.1f/s, held back by the limit %.1f/s%s\n", Name.c_str(),
        ModeName(Status.Mode), Status.MaxMilliFps == 0 ? "auto" : QByteArray::number(Status.MaxMilliFps / 1000).constData(), Status.Wanted, Status.Applied, Status.AdaptiveOk ? "yes" : "no", Status.RefreshMilliHz / 1000.0, Rate,
        Status.GameMilliHz / 1000.0, Now > 0 ? Shown * 1000.0 / Now : 0.0, Now > 0 ? Dropped * 1000.0 / Now : 0.0, Status.Unthrottled != 0 ? ", fast forward: the limit is not applied" : "");
}
} // namespace Vortex
