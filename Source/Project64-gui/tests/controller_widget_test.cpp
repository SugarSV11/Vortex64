// The vector N64 controller widget: geometry at 100/125/150/200 %, the states, and PNGs to look at (VX_SHOTS=dir).
#include "../../Project64-input/N64ControllerWidget.h"
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <cstdio>
#include <cstdlib>

static int g_Fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_Fail++; std::printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

int main(int argc, char ** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication App(argc, argv);
    if (std::getenv("VX_DARK"))
    {
        QPalette D;
        D.setColor(QPalette::Window, QColor(0x14, 0x16, 0x1b)); D.setColor(QPalette::WindowText, QColor(0xe6, 0xe8, 0xee));
        D.setColor(QPalette::AlternateBase, QColor(0x2a, 0x2e, 0x38)); D.setColor(QPalette::Link, QColor(0x4f, 0x8c, 0xff));
        App.setPalette(D);
    }
    const QPainterPath Body = N64ControllerWidget::BodyPath();
    const QRectF Bounds = Body.boundingRect();
    CHECK(Bounds.left() > 0 && Bounds.right() < 400 && Bounds.top() > 36 && Bounds.bottom() < 298, "body inside canvas");
    CHECK(qAbs(Bounds.center().x() - 200) < 0.5, "body symmetric");
    // three grips: a horizontal line near the bottom crosses the body in three separate runs
    int Runs = 0; bool In = false;
    for (int x = 0; x < 400; x++) { bool I = Body.contains(QPointF(x + 0.5, 250)); if (I && !In) Runs++; In = I; }
    CHECK(Runs == 3, "three grips at y=250 (%d)", Runs);
    // controls
    const QRectF Canvas(0, 36, 400, 262);
    for (int i = 0; i < (int)N64Control::Count; i++)
    {
        const N64Control C = (N64Control)i;
        const QRectF R = N64ControllerWidget::ControlBounds(C);
        CHECK(!R.isEmpty() && Canvas.contains(R), "control %d in canvas", i);
        // every control, L and R too (they are bands of the outline), is inside the body
        const bool Shoulder = C == N64Control::L || C == N64Control::R;
        if (!Shoulder)
            CHECK(Body.contains(R.center()) && Body.contains(R.topLeft() + QPointF(1, 1)) && Body.contains(R.bottomRight() - QPointF(1, 1)), "control %d inside body", i);
        else
            CHECK(Body.boundingRect().adjusted(-0.01, -0.01, 0.01, 0.01).contains(R) && Body.contains(R.center() + QPointF(0, 3)) && R.top() < 70 && R.height() < 40, "the shoulder %d is a band of the top edge, not an ear (%.0f x %.0f)", i, R.width(), R.height());
        for (int j = i + 1; j < (int)N64Control::Count; j++)
        {
            const QRectF O = N64ControllerWidget::ControlBounds((N64Control)j);
            const bool Skip = (C >= N64Control::StickUp && C <= N64Control::StickRight) != ((N64Control)j >= N64Control::StickUp && (N64Control)j <= N64Control::StickRight) && false;
            if (!Skip) CHECK(!R.intersects(O), "controls %d and %d overlap", i, j);
        }
    }
    CHECK(qAbs(N64ControllerWidget::StickCenter().x() - 200) < 0.5, "stick centered");
    CHECK(N64ControllerWidget::ControlBounds(N64Control::DPadLeft).center().x() < 200 && N64ControllerWidget::ControlBounds(N64Control::A).center().x() > 200, "d-pad left, A right");
    CHECK(N64ControllerWidget::ControlBounds(N64Control::L).center().x() < 200 && N64ControllerWidget::ControlBounds(N64Control::R).center().x() > 200, "L left, R right");
    CHECK(qAbs(N64ControllerWidget::ControlBounds(N64Control::Z).center().x() - 200) < 0.5, "Z centered");
    // states
    N64ControllerWidget W;
    W.SetHover(N64Control::A);
    CHECK(W.StateOf(N64Control::A) == N64ControlState::Hover, "hover");
    W.SetCapturing(N64Control::A);
    CHECK(W.StateOf(N64Control::A) == N64ControlState::Capturing, "capturing beats hover");
    W.SetPressed(N64Control::A, true);
    CHECK(W.StateOf(N64Control::A) == N64ControlState::Pressed, "pressed beats capturing");
    W.ClearPressed(); W.SetCapturing(N64Control::None); W.SetHover(N64Control::None);
    CHECK(W.StateOf(N64Control::A) == N64ControlState::Normal, "normal");
    // aspect ratio at the scales
    const char * Dir = std::getenv("VX_SHOTS");
    const double Scales[] = {1.0, 1.25, 1.5, 2.0};
    for (double S : Scales)
    {
        for (int Variant = 0; Variant < 3; Variant++)
        {
            N64ControllerWidget Wd;
            Wd.resize(int(520 * S), int(340 * S));
            Wd.ensurePolished();
            const QRectF CR = Wd.CanvasRect();
            CHECK(qAbs(CR.width() / CR.height() - N64ControllerWidget::CanvasSize().width() / N64ControllerWidget::CanvasSize().height()) < 0.02, "aspect at %.2f", S);
            CHECK(CR.left() >= -0.5 && CR.right() <= Wd.width() + 0.5 && CR.top() >= -0.5 && CR.bottom() <= Wd.height() + 0.5, "canvas inside widget");
            Wd.resize(int(300 * S), int(500 * S)); // a tall, narrow space: still the same ratio, not cut
            const QRectF Tall = Wd.CanvasRect();
            CHECK(qAbs(Tall.width() / Tall.height() - N64ControllerWidget::CanvasSize().width() / N64ControllerWidget::CanvasSize().height()) < 0.02 && Tall.right() <= Wd.width() + 0.5, "tall aspect at %.2f", S);
            Wd.resize(int(520 * S), int(340 * S));
            if (Variant == 1) { Wd.SetHover(N64Control::A); }
            if (Variant == 2) { Wd.SetCapturing(N64Control::CLeft); Wd.SetPressed(N64Control::B, true); Wd.SetPressed(N64Control::L, true); Wd.SetPressed(N64Control::DPadUp, true); Wd.SetStick(QPointF(0.7, -0.5)); }
            if (Dir)
            {
                Wd.setAutoFillBackground(true); QImage Img = Wd.grab().toImage();
                Img.save(QString("%1/controller-%2-v%3.png").arg(Dir).arg(int(S * 100)).arg(Variant));
            }
        }
    }
    std::printf(g_Fail ? "FAILED %d\n" : "PASS\n", g_Fail);
    return g_Fail ? 1 : 0;
}
