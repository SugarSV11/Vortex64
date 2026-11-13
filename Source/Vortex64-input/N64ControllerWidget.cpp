#include "N64ControllerWidget.h"

#include <QFont>
#include <QLinearGradient>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QtMath>
#include <cmath>

namespace
{
// the drawing lives in a 400 x 300 plane; the canvas is the part of it that holds the controller (a margin of 4 around it)
const qreal CanvasWidth = 400, CanvasTop = 36, CanvasHeight = 262;

// ---- the silhouette: one closed outline, symmetric, of cubic curves (the right half; the left is its mirror image)
struct Cubic
{
    QPointF C1, C2, End;
};

QPointF Mirror(const QPointF & P)
{
    return QPointF(CanvasWidth - P.x(), P.y());
}

QPainterPath MakeBody()
{
    const QPointF Top(200, 62);
    // from the middle of the top edge, clockwise, to the middle of the bottom of the center grip
    const Cubic Right[] = {
        {{240, 58}, {292, 55}, {330, 61}},    // the top edge, nearly flat, rising a little to the corner
        {{354, 65}, {366, 82}, {368, 110}},   // the round corner of the top, down the side
        {{371, 158}, {364, 212}, {348, 252}}, // the outer edge of the right grip: it narrows towards the end
        {{341, 272}, {328, 286}, {314, 285}}, // the rounded end of the grip
        {{301, 284}, {296, 270}, {297, 252}}, // and back up its inner edge
        {{298, 234}, {298, 218}, {290, 208}}, //
        {{282, 199}, {267, 198}, {264, 213}}, // the round top of the gap between the grips
        {{261, 230}, {248, 250}, {240, 266}}, // the outer edge of the center grip
        {{235, 280}, {228, 288}, {216, 288}}, // its end
        {{210, 288}, {205, 288}, {200, 288}},
    };
    QPainterPath P(Top);
    for (const Cubic & C : Right)
    {
        P.cubicTo(C.C1, C.C2, C.End);
    }
    // the left half: the same curves from the bottom to the top, mirrored and walked backwards
    for (int i = (int)(sizeof(Right) / sizeof(Right[0])) - 1; i >= 0; i--)
    {
        const QPointF End = i == 0 ? Top : Right[i - 1].End;
        P.cubicTo(Mirror(Right[i].C2), Mirror(Right[i].C1), Mirror(End));
    }
    P.closeSubpath();
    return P;
}

const QPointF DPadCenter(98, 134);
const qreal DPadArm = 28, DPadWidth = 20;
const QPointF StickPos(200, 170);
const qreal StickWell = 22, StickKnob = 14, StickMarkerRadius = 33, StickMove = 9;
const QPointF StartCenter(200, 100);
const qreal StartRadius = 8;
const QPointF BCenter(273, 160), ACenter(315, 176);
const qreal BRadius = 14, ARadius = 17;
const QPointF CCenter(311, 118);
const qreal CArm = 23, CRadius = 11;
const QRectF ZRect(176, 250, 48, 20);

// L and R are bands along the top corners of the body (the shape of the body itself, cut by a band): part of the outline, not
// something that sticks out of it
QPainterPath MakeShoulder(bool Left)
{
    QPainterPath Band;
    Band.addRoundedRect(QRectF(Left ? 62 : 238, 40, 100, 35), 10, 10);
    return MakeBody().intersected(Band);
}

QRectF DPadArmRect(N64Control C)
{
    const qreal H = DPadWidth / 2;
    switch (C)
    {
    case N64Control::DPadUp: return QRectF(DPadCenter.x() - H, DPadCenter.y() - DPadArm - H + 2, DPadWidth, DPadArm - 2);
    case N64Control::DPadDown: return QRectF(DPadCenter.x() - H, DPadCenter.y() + H, DPadWidth, DPadArm - 2);
    case N64Control::DPadLeft: return QRectF(DPadCenter.x() - DPadArm - H + 2, DPadCenter.y() - H, DPadArm - 2, DPadWidth);
    default: return QRectF(DPadCenter.x() + H, DPadCenter.y() - H, DPadArm - 2, DPadWidth);
    }
}

QPointF CCenterOf(N64Control C)
{
    switch (C)
    {
    case N64Control::CUp: return CCenter + QPointF(0, -CArm);
    case N64Control::CDown: return CCenter + QPointF(0, CArm);
    case N64Control::CLeft: return CCenter + QPointF(-CArm, 0);
    default: return CCenter + QPointF(CArm, 0);
    }
}

// the angle (radians, counter clockwise from the right) of a direction of the stick
qreal StickAngle(N64Control C)
{
    switch (C)
    {
    case N64Control::StickUp: return M_PI / 2;
    case N64Control::StickDown: return -M_PI / 2;
    case N64Control::StickLeft: return M_PI;
    default: return 0;
    }
}

QPointF StickMarker(N64Control C)
{
    const qreal A = StickAngle(C);
    return StickPos + QPointF(qCos(A), -qSin(A)) * StickMarkerRadius;
}

QPainterPath Triangle(const QPointF & Tip, qreal Angle, qreal Size)
{
    // an arrow head pointing at Angle
    QPainterPath P;
    const QPointF Dir(qCos(Angle), -qSin(Angle)), Side(-Dir.y(), Dir.x());
    P.moveTo(Tip + Dir * Size);
    P.lineTo(Tip - Dir * Size * 0.6 + Side * Size * 0.9);
    P.lineTo(Tip - Dir * Size * 0.6 - Side * Size * 0.9);
    P.closeSubpath();
    return P;
}

const QColor ColorA(0x5b, 0x8b, 0xd9), ColorB(0x5f, 0xb9, 0x7a), ColorC(0xd9, 0xb4, 0x4a), ColorStart(0xd9, 0x6a, 0x6a);
} // namespace

N64ControllerWidget::N64ControllerWidget(QWidget * Parent) :
    QWidget(Parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(minimumSizeHint());
    setProperty("vxHover", -1);
    setProperty("vxCapturing", -1);
    ClearPressed();
}

QSize N64ControllerWidget::sizeHint() const
{
    return QSize(520, 340);
}

QSize N64ControllerWidget::minimumSizeHint() const
{
    return QSize(260, 170);
}

QSizeF N64ControllerWidget::CanvasSize()
{
    return QSizeF(CanvasWidth, CanvasHeight);
}

QPainterPath N64ControllerWidget::BodyPath()
{
    static const QPainterPath Path = MakeBody();
    return Path;
}

QPainterPath N64ControllerWidget::ShoulderPath(N64Control Control)
{
    static const QPainterPath Left = MakeShoulder(true), Right = MakeShoulder(false);
    return Control == N64Control::L ? Left : Right;
}

QPointF N64ControllerWidget::StickCenter()
{
    return StickPos;
}

qreal N64ControllerWidget::StickWellRadius()
{
    return StickWell;
}

qreal N64ControllerWidget::StickTravel()
{
    return StickMove;
}

QRectF N64ControllerWidget::ControlBounds(N64Control Control)
{
    switch (Control)
    {
    case N64Control::DPadUp:
    case N64Control::DPadDown:
    case N64Control::DPadLeft:
    case N64Control::DPadRight:
        return DPadArmRect(Control);
    case N64Control::StickUp:
    case N64Control::StickDown:
    case N64Control::StickLeft:
    case N64Control::StickRight:
    {
        const QPointF M = StickMarker(Control);
        return QRectF(M.x() - 7, M.y() - 7, 14, 14);
    }
    case N64Control::CUp:
    case N64Control::CDown:
    case N64Control::CLeft:
    case N64Control::CRight:
    {
        const QPointF C = CCenterOf(Control);
        return QRectF(C.x() - CRadius, C.y() - CRadius, CRadius * 2, CRadius * 2);
    }
    case N64Control::B: return QRectF(BCenter.x() - BRadius, BCenter.y() - BRadius, BRadius * 2, BRadius * 2);
    case N64Control::A: return QRectF(ACenter.x() - ARadius, ACenter.y() - ARadius, ARadius * 2, ARadius * 2);
    case N64Control::Start: return QRectF(StartCenter.x() - StartRadius, StartCenter.y() - StartRadius, StartRadius * 2, StartRadius * 2);
    case N64Control::Z: return ZRect;
    case N64Control::L: return ShoulderPath(N64Control::L).boundingRect();
    case N64Control::R: return ShoulderPath(N64Control::R).boundingRect();
    default: break;
    }
    return QRectF();
}

void N64ControllerWidget::SetHover(N64Control Control)
{
    if (m_Hover != Control)
    {
        m_Hover = Control;
        setProperty("vxHover", (int)Control); // (what is lit, for the tests and the accessibility tools)
        update();
    }
}

void N64ControllerWidget::SetCapturing(N64Control Control)
{
    if (m_Capturing != Control)
    {
        m_Capturing = Control;
        setProperty("vxCapturing", (int)Control);
        update();
    }
}

void N64ControllerWidget::SetPressed(N64Control Control, bool On)
{
    if (Control != N64Control::None && Control != N64Control::Count && m_Pressed[(int)Control] != On)
    {
        m_Pressed[(int)Control] = On;
        unsigned Mask = 0;
        for (int i = 0; i < (int)N64Control::Count; i++)
        {
            Mask |= m_Pressed[i] ? (1u << i) : 0u;
        }
        setProperty("vxPressed", Mask);
        update();
    }
}

void N64ControllerWidget::ClearPressed()
{
    for (bool & P : m_Pressed)
    {
        P = false;
    }
    m_Stick = QPointF();
    setProperty("vxPressed", 0u);
    setProperty("vxStick", m_Stick);
    update();
}

void N64ControllerWidget::SetStick(const QPointF & Position)
{
    if (m_Stick != Position)
    {
        m_Stick = Position;
        setProperty("vxStick", m_Stick);
        update();
    }
}

void N64ControllerWidget::SetEnabledLook(bool On)
{
    if (m_Dim == On)
    {
        m_Dim = !On;
        update();
    }
}

N64ControlState N64ControllerWidget::StateOf(N64Control Control) const
{
    if (Control == N64Control::None || Control == N64Control::Count)
    {
        return N64ControlState::Normal;
    }
    if (m_Pressed[(int)Control])
    {
        return N64ControlState::Pressed;
    }
    if (m_Capturing == Control)
    {
        return N64ControlState::Capturing;
    }
    return m_Hover == Control ? N64ControlState::Hover : N64ControlState::Normal;
}

QRectF N64ControllerWidget::CanvasRect() const
{
    const qreal Scale = qMin(width() / CanvasWidth, height() / CanvasHeight);
    const QSizeF Size(CanvasWidth * Scale, CanvasHeight * Scale);
    return QRectF((width() - Size.width()) / 2, (height() - Size.height()) / 2, Size.width(), Size.height());
}

void N64ControllerWidget::paintEvent(QPaintEvent *)
{
    QPainter P(this);
    Render(P, CanvasRect());
}

void N64ControllerWidget::Render(QPainter & P, const QRectF & Target) const
{
    P.save();
    P.setRenderHint(QPainter::Antialiasing);
    P.setRenderHint(QPainter::TextAntialiasing);
    const qreal Scale = Target.width() / CanvasWidth;
    P.translate(Target.topLeft());
    P.scale(Scale, Scale);
    P.translate(0, -CanvasTop);

    const QPalette & Pal = palette();
    const qreal Fade = m_Dim ? 0.45 : 1.0;
    auto Faded = [&](QColor C, int Alpha = 255) {
        C.setAlpha(int(Alpha * Fade));
        return C;
    };
    const QColor Text = Pal.color(QPalette::WindowText);
    const QColor Accent = Pal.color(QPalette::Link);
    const QColor BodyBase = Pal.color(QPalette::AlternateBase);
    const QColor Outline = Faded(Text, 125);
    const QColor Detail = Faded(Text, 55);
    const QColor Well = Pal.color(QPalette::Window).darker(115);

    // ---- the pieces of a control in each state
    auto Ring = [&](N64Control C, qreal Width = 1.8) -> QPen {
        switch (StateOf(C))
        {
        case N64ControlState::Hover: return QPen(Faded(Accent), Width + 0.8);
        case N64ControlState::Capturing: return QPen(Faded(Accent), Width + 1.6);
        case N64ControlState::Pressed: return QPen(Faded(Pal.color(QPalette::Link).lighter(150)), Width + 0.6);
        default: break;
        }
        return QPen(Outline, Width);
    };
    auto Fill = [&](N64Control C, const QColor & Base) -> QBrush {
        switch (StateOf(C))
        {
        case N64ControlState::Pressed: return Faded(Pal.color(QPalette::Link).lighter(112));
        case N64ControlState::Capturing: return Faded(Accent, 120);
        case N64ControlState::Hover: return Faded(Base, 120);
        default: break;
        }
        return Faded(Base, 55);
    };
    auto Glow = [&](N64Control C, const QPainterPath & Shape) {
        // the control that waits for an input: a halo around it (more than the colour: it must be seen at once)
        if (StateOf(C) == N64ControlState::Capturing)
        {
            P.setBrush(Qt::NoBrush);
            P.setPen(QPen(Faded(Accent, 70), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            P.drawPath(Shape);
        }
    };
    auto Round = [&](N64Control C, const QPointF & Center, qreal R, const QColor & Base) {
        QPainterPath Shape;
        Shape.addEllipse(Center, R, R);
        Glow(C, Shape);
        P.setPen(Ring(C));
        P.setBrush(Fill(C, Base));
        P.drawPath(Shape);
    };
    auto Bar = [&](N64Control C, const QRectF & Rect, qreal Radius, const QColor & Base) {
        QPainterPath Shape;
        Shape.addRoundedRect(Rect, Radius, Radius);
        Glow(C, Shape);
        P.setPen(Ring(C));
        P.setBrush(Fill(C, Base));
        P.drawPath(Shape);
    };

    const QColor Grey = Faded(Text, 255);

    // ---- the body
    const QPainterPath Body = BodyPath();
    QLinearGradient Shade(0, 40, 0, 292);
    Shade.setColorAt(0, Faded(BodyBase.lighter(112)));
    Shade.setColorAt(1, Faded(BodyBase.darker(112)));
    P.setPen(QPen(Outline, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    P.setBrush(Shade);
    P.drawPath(Body);
    // a thin line inside the edge, as the rim of a plastic piece
    QPainterPathStroker Inset;
    Inset.setWidth(7);
    P.setPen(QPen(Detail, 1.2));
    P.setBrush(Qt::NoBrush);
    P.save();
    P.setClipPath(Body);
    P.drawPath(Body.translated(0, 0));
    P.setPen(QPen(Detail, 1.2));
    P.drawPath(Inset.createStroke(Body).intersected(Body).simplified().subtracted(Body.translated(0, 0)));
    P.restore();

    // ---- L and R: the bands of the top corners, in the outline of the body
    for (N64Control Shoulder : {N64Control::L, N64Control::R})
    {
        const QPainterPath Shape = N64ControllerWidget::ShoulderPath(Shoulder);
        Glow(Shoulder, Shape);
        P.setPen(Ring(Shoulder));
        P.setBrush(Fill(Shoulder, Grey));
        P.drawPath(Shape);
    }

    // ---- Z, under the center grip
    Bar(N64Control::Z, ZRect, 7, Grey);

    // ---- the D-pad: four arms and the middle
    {
        QPainterPath Cross;
        const qreal H = DPadWidth / 2;
        Cross.addRoundedRect(QRectF(DPadCenter.x() - H, DPadCenter.y() - H, DPadWidth, DPadWidth), 3, 3);
        P.setPen(QPen(Outline, 1.8));
        P.setBrush(Faded(BodyBase.darker(112)));
        P.drawPath(Cross);
        const N64Control Arms[] = {N64Control::DPadUp, N64Control::DPadDown, N64Control::DPadLeft, N64Control::DPadRight};
        for (N64Control Arm : Arms)
        {
            Bar(Arm, DPadArmRect(Arm), 4, Grey);
        }
        // the arrow on each arm
        P.setPen(Qt::NoPen);
        P.setBrush(Faded(Text, 110));
        const qreal Angles[] = {M_PI / 2, -M_PI / 2, M_PI, 0};
        for (int i = 0; i < 4; i++)
        {
            const QRectF R = DPadArmRect(Arms[i]);
            P.drawPath(Triangle(R.center(), Angles[i], 4.2));
        }
    }

    // ---- the stick: the well, the marks of the four directions and the knob
    {
        P.setPen(QPen(Outline, 1.8));
        P.setBrush(Faded(Well));
        P.drawEllipse(StickPos, StickWell, StickWell);
        P.setPen(QPen(Detail, 1.2));
        P.setBrush(Qt::NoBrush);
        P.drawEllipse(StickPos, StickWell - 6, StickWell - 6);
        const N64Control Dirs[] = {N64Control::StickUp, N64Control::StickDown, N64Control::StickLeft, N64Control::StickRight};
        for (N64Control D : Dirs)
        {
            const QPainterPath Mark = Triangle(StickMarker(D), StickAngle(D), 5.2);
            Glow(D, Mark);
            P.setPen(Ring(D, 1.2));
            P.setBrush(StateOf(D) == N64ControlState::Normal ? QBrush(Faded(Text, 70)) : Fill(D, Text));
            P.drawPath(Mark);
        }
        const QPointF Knob = StickPos + QPointF(m_Stick.x(), -m_Stick.y()) * StickMove;
        P.setPen(QPen(Outline, 1.8));
        QLinearGradient KnobShade(Knob.x(), Knob.y() - StickKnob, Knob.x(), Knob.y() + StickKnob);
        KnobShade.setColorAt(0, Faded(BodyBase.lighter(155)));
        KnobShade.setColorAt(1, Faded(BodyBase.lighter(118)));
        P.setBrush(KnobShade);
        P.drawEllipse(Knob, StickKnob, StickKnob);
        P.setPen(QPen(Detail, 1.2));
        P.setBrush(Qt::NoBrush);
        P.drawEllipse(Knob, StickKnob - 6, StickKnob - 6);
    }

    // ---- Start, B, A
    Round(N64Control::Start, StartCenter, StartRadius, ColorStart);
    Round(N64Control::B, BCenter, BRadius, ColorB);
    Round(N64Control::A, ACenter, ARadius, ColorA);

    // ---- the C buttons
    {
        const N64Control Cs[] = {N64Control::CUp, N64Control::CDown, N64Control::CLeft, N64Control::CRight};
        const qreal Angles[] = {M_PI / 2, -M_PI / 2, M_PI, 0};
        for (int i = 0; i < 4; i++)
        {
            Round(Cs[i], CCenterOf(Cs[i]), CRadius, ColorC);
            P.setPen(Qt::NoPen);
            P.setBrush(Faded(StateOf(Cs[i]) == N64ControlState::Pressed ? Pal.color(QPalette::Window) : ColorC.lighter(110), 235));
            P.drawPath(Triangle(CCenterOf(Cs[i]), Angles[i], 4.0));
        }
    }

    // ---- the letters
    QFont Font = font();
    Font.setPixelSize(12);
    Font.setBold(true);
    P.setFont(Font);
    auto Letter = [&](const QRectF & R, const char * Text_, N64Control C) {
        P.setPen(StateOf(C) == N64ControlState::Pressed ? Faded(Pal.color(QPalette::Window)) : Faded(Text, 215));
        P.drawText(R, Qt::AlignCenter, QString::fromLatin1(Text_));
    };
    Letter(ControlBounds(N64Control::A), "A", N64Control::A);
    Letter(ControlBounds(N64Control::B), "B", N64Control::B);
    Font.setPixelSize(11);
    P.setFont(Font);
    Letter(ControlBounds(N64Control::L), "L", N64Control::L);
    Letter(ControlBounds(N64Control::R), "R", N64Control::R);
    Letter(ZRect, "Z", N64Control::Z);
    Font.setPixelSize(7);
    Font.setBold(false);
    P.setFont(Font);
    P.setPen(Faded(Text, 140));
    P.drawText(QRectF(StartCenter.x() - 24, StartCenter.y() + StartRadius + 2, 48, 10), Qt::AlignCenter, QStringLiteral("START"));
    P.restore();
}
