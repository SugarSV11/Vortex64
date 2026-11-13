#pragma once

#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QWidget>

// The N64 controller of the input window, drawn with vectors (QPainterPath, no image): the silhouette of the three grips, the D-pad, the
// stick, the C buttons, A, B, Start, the triggers L, R and Z. It is the one drawing of the window: the page of the assignment lights up
// the control that is under the pointer or that waits for an input, the page of the test lights up the ones that are down and moves the
// stick.
//
// The drawing is made on a canvas of fixed proportions (CanvasSize) that is scaled to the widget keeping them, so it never deforms.
// The positions of the controls are in the coordinates of the canvas and are public (ControlBounds, BodyPath...) for the tests.
enum class N64Control : int
{
    None = -1,
    // the order of the rows of the assignment (the order the sequential assignment walks them)
    DPadUp, DPadDown, DPadLeft, DPadRight,
    StickUp, StickDown, StickLeft, StickRight,
    CUp, CDown, CLeft, CRight,
    B, A, Start, Z, L, R,
    Count
};

enum class N64ControlState
{
    Normal,
    Hover,
    Capturing,
    Pressed
};

class N64ControllerWidget : public QWidget
{
public:
    explicit N64ControllerWidget(QWidget * Parent = nullptr);

    // ---- state shown (the strongest of a control is the one that is drawn: Pressed, then Capturing, then Hover)
    void SetHover(N64Control Control);
    void SetCapturing(N64Control Control);
    void SetPressed(N64Control Control, bool On);
    void ClearPressed();
    // the stick as the test sees it: -1..1 on each axis (x to the right, y up)
    void SetStick(const QPointF & Position);
    // a control that is not an N64 controller (none, mouse): the drawing is dimmed
    void SetEnabledLook(bool On);
    N64ControlState StateOf(N64Control Control) const;
    N64Control Hover() const
    {
        return m_Hover;
    }
    N64Control Capturing() const
    {
        return m_Capturing;
    }
    QPointF Stick() const
    {
        return m_Stick;
    }

    // ---- the canvas and the shapes (canvas coordinates)
    static QSizeF CanvasSize();
    static QPainterPath BodyPath();
    // the band of the top corner of the body that is the trigger L or R
    static QPainterPath ShoulderPath(N64Control Control);
    // the area a control occupies, with its marks (the arms of the D-pad, the ring of the stick for its directions...)
    static QRectF ControlBounds(N64Control Control);
    static QPointF StickCenter();
    static qreal StickWellRadius();
    static qreal StickTravel(); // how far the knob goes from the center for 1.0
    // where the canvas is inside this widget (aspect ratio kept, centered)
    QRectF CanvasRect() const;
    // The drawing on a painter, in the rectangle (the widget draws itself with this; the tests and the captures use it too)
    void Render(QPainter & Painter, const QRectF & Target) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent * Event) override;

private:
    N64Control m_Hover = N64Control::None;
    N64Control m_Capturing = N64Control::None;
    bool m_Pressed[(int)N64Control::Count];
    QPointF m_Stick;
    bool m_Dim = false;
};
