#include "Icons.h"
#include "Theme.h"

#include <QHash>
#include <QPainterPath>
#include <QtMath>

namespace Vortex
{
namespace
{
void StrokePath(QPainter & P, const QPainterPath & Path, qreal Width)
{
    QPen Pen = P.pen();
    Pen.setWidthF(Width);
    Pen.setCapStyle(Qt::RoundCap);
    Pen.setJoinStyle(Qt::RoundJoin);
    P.setPen(Pen);
    P.drawPath(Path);
}

void Draw(QPainter & P, Icon Which)
{
    P.setBrush(Qt::NoBrush);
    QPainterPath Path;
    auto Rect = [&](qreal X, qreal Y, qreal W, qreal H, qreal R = 2) { Path.addRoundedRect(QRectF(X, Y, W, H), R, R); };
    switch (Which)
    {
    case Icon::Library:
        Rect(3.5, 3.5, 7, 7);
        Rect(13.5, 3.5, 7, 7);
        Rect(3.5, 13.5, 7, 7);
        Rect(13.5, 13.5, 7, 7);
        break;
    case Icon::Recent:
        Path.addEllipse(QPointF(12, 12), 8.5, 8.5);
        Path.moveTo(12, 7);
        Path.lineTo(12, 12);
        Path.lineTo(15.5, 14);
        break;
    case Icon::Favorites:
    case Icon::Star:
    case Icon::StarOutline:
    {
        for (int i = 0; i < 10; i++)
        {
            const qreal Angle = -M_PI / 2 + i * M_PI / 5;
            const qreal Radius = (i % 2 == 0) ? 9.5 : 4.2;
            const QPointF Point(12 + Radius * qCos(Angle), 12.7 + Radius * qSin(Angle));
            if (i == 0)
            {
                Path.moveTo(Point);
            }
            else
            {
                Path.lineTo(Point);
            }
        }
        Path.closeSubpath();
        if (Which == Icon::Star)
        {
            P.setBrush(P.pen().color());
        }
        break;
    }
    case Icon::Disk:
        Path.addEllipse(QPointF(12, 12), 9, 9);
        Path.addEllipse(QPointF(12, 12), 2.6, 2.6);
        Path.moveTo(12, 3);
        Path.arcTo(QRectF(3, 3, 18, 18), 90, 25);
        break;
    case Icon::Settings:
    {
        Path.addEllipse(QPointF(12, 12), 3.2, 3.2);
        for (int i = 0; i < 8; i++)
        {
            const qreal A = i * M_PI / 4;
            Path.moveTo(12 + 6.6 * qCos(A), 12 + 6.6 * qSin(A));
            Path.lineTo(12 + 9.6 * qCos(A), 12 + 9.6 * qSin(A));
        }
        Path.addEllipse(QPointF(12, 12), 6.6, 6.6);
        break;
    }
    case Icon::Debugger:
        Path.addEllipse(QPointF(12, 13.5), 5.2, 6.4);
        Path.moveTo(12, 7.2);
        Path.lineTo(12, 19.8);
        Path.moveTo(6.8, 11);
        Path.lineTo(3, 9.5);
        Path.moveTo(6.8, 15.5);
        Path.lineTo(3, 17.5);
        Path.moveTo(17.2, 11);
        Path.lineTo(21, 9.5);
        Path.moveTo(17.2, 15.5);
        Path.lineTo(21, 17.5);
        Path.moveTo(9.5, 8);
        Path.lineTo(7.5, 4.5);
        Path.moveTo(14.5, 8);
        Path.lineTo(16.5, 4.5);
        break;
    case Icon::About:
    case Icon::Info:
        Path.addEllipse(QPointF(12, 12), 9, 9);
        Path.moveTo(12, 11);
        Path.lineTo(12, 16.5);
        Path.moveTo(12, 7.6);
        Path.lineTo(12, 7.7);
        break;
    case Icon::Search:
        Path.addEllipse(QPointF(10.5, 10.5), 6.2, 6.2);
        Path.moveTo(15.2, 15.2);
        Path.lineTo(20.5, 20.5);
        break;
    case Icon::ViewGrid:
        Rect(4, 4, 6.5, 6.5, 1.5);
        Rect(13.5, 4, 6.5, 6.5, 1.5);
        Rect(4, 13.5, 6.5, 6.5, 1.5);
        Rect(13.5, 13.5, 6.5, 6.5, 1.5);
        break;
    case Icon::ViewList:
        for (int i = 0; i < 3; i++)
        {
            Path.moveTo(4, 6 + i * 6);
            Path.lineTo(4.1, 6 + i * 6);
            Path.moveTo(8, 6 + i * 6);
            Path.lineTo(20, 6 + i * 6);
        }
        break;
    case Icon::ViewCompact:
        for (int i = 0; i < 5; i++)
        {
            Path.moveTo(4, 4.5 + i * 3.75);
            Path.lineTo(20, 4.5 + i * 3.75);
        }
        break;
    case Icon::Folder:
        Path.moveTo(3, 7);
        Path.lineTo(3, 18.5);
        Path.lineTo(21, 18.5);
        Path.lineTo(21, 8.5);
        Path.lineTo(11.5, 8.5);
        Path.lineTo(9.5, 5.5);
        Path.lineTo(3, 5.5);
        Path.closeSubpath();
        break;
    case Icon::Play:
        Path.moveTo(7, 4.5);
        Path.lineTo(19, 12);
        Path.lineTo(7, 19.5);
        Path.closeSubpath();
        P.setBrush(P.pen().color());
        break;
    case Icon::Pause:
        Rect(6, 5, 4, 14, 1);
        Rect(14, 5, 4, 14, 1);
        break;
    case Icon::Stop:
        Rect(6, 6, 12, 12, 2);
        break;
    case Icon::Refresh:
        Path.moveTo(19.5, 12);
        Path.arcTo(QRectF(4.5, 4.5, 15, 15), 0, 300);
        Path.moveTo(19.8, 4.5);
        Path.lineTo(19.8, 9.5);
        Path.lineTo(14.8, 9.5);
        break;
    case Icon::Plus:
        Path.moveTo(12, 5);
        Path.lineTo(12, 19);
        Path.moveTo(5, 12);
        Path.lineTo(19, 12);
        break;
    case Icon::Minus:
        Path.moveTo(5, 12);
        Path.lineTo(19, 12);
        break;
    case Icon::Close:
        Path.moveTo(6, 6);
        Path.lineTo(18, 18);
        Path.moveTo(18, 6);
        Path.lineTo(6, 18);
        break;
    case Icon::Check:
        Path.moveTo(5.5, 12.5);
        Path.lineTo(10, 17);
        Path.lineTo(18.5, 7.5);
        break;
    case Icon::Camera:
        Rect(3, 7, 18, 12, 2.5);
        Path.addEllipse(QPointF(12, 13), 3.6, 3.6);
        Path.moveTo(8.5, 7);
        Path.lineTo(10, 4.5);
        Path.lineTo(14, 4.5);
        Path.lineTo(15.5, 7);
        break;
    case Icon::Save:
        Path.moveTo(5, 3.5);
        Path.lineTo(17, 3.5);
        Path.lineTo(20.5, 7);
        Path.lineTo(20.5, 20.5);
        Path.lineTo(3.5, 20.5);
        Path.lineTo(3.5, 5);
        Path.closeSubpath();
        Rect(7, 3.5, 8, 5, 0.5);
        Rect(7, 13, 10, 7.5, 0.5);
        break;
    case Icon::Load:
        Path.moveTo(12, 3.5);
        Path.lineTo(12, 15);
        Path.moveTo(7.5, 10.5);
        Path.lineTo(12, 15);
        Path.lineTo(16.5, 10.5);
        Path.moveTo(4, 17);
        Path.lineTo(4, 20.5);
        Path.lineTo(20, 20.5);
        Path.lineTo(20, 17);
        break;
    case Icon::Fullscreen:
        Path.moveTo(4, 9);
        Path.lineTo(4, 4);
        Path.lineTo(9, 4);
        Path.moveTo(15, 4);
        Path.lineTo(20, 4);
        Path.lineTo(20, 9);
        Path.moveTo(20, 15);
        Path.lineTo(20, 20);
        Path.lineTo(15, 20);
        Path.moveTo(9, 20);
        Path.lineTo(4, 20);
        Path.lineTo(4, 15);
        break;
    case Icon::Gamepad:
        Path.moveTo(7, 7);
        Path.lineTo(17, 7);
        Path.cubicTo(20.5, 7, 22, 12, 21, 16);
        Path.cubicTo(20.4, 18.4, 18, 18.6, 16.8, 16.8);
        Path.lineTo(15.4, 14.6);
        Path.lineTo(8.6, 14.6);
        Path.lineTo(7.2, 16.8);
        Path.cubicTo(6, 18.6, 3.6, 18.4, 3, 16);
        Path.cubicTo(2, 12, 3.5, 7, 7, 7);
        Path.moveTo(8, 9.5);
        Path.lineTo(8, 12.5);
        Path.moveTo(6.5, 11);
        Path.lineTo(9.5, 11);
        Path.moveTo(15.6, 10.2);
        Path.lineTo(15.7, 10.3);
        Path.moveTo(17.6, 12);
        Path.lineTo(17.7, 12.1);
        break;
    case Icon::Chip:
        Rect(6.5, 6.5, 11, 11, 2);
        Rect(9.5, 9.5, 5, 5, 1);
        for (int i = 0; i < 3; i++)
        {
            const qreal V = 9 + i * 3;
            Path.moveTo(V, 3.5);
            Path.lineTo(V, 6.5);
            Path.moveTo(V, 17.5);
            Path.lineTo(V, 20.5);
            Path.moveTo(3.5, V);
            Path.lineTo(6.5, V);
            Path.moveTo(17.5, V);
            Path.lineTo(20.5, V);
        }
        break;
    case Icon::Audio:
        Path.moveTo(4, 9.5);
        Path.lineTo(8, 9.5);
        Path.lineTo(13, 5.5);
        Path.lineTo(13, 18.5);
        Path.lineTo(8, 14.5);
        Path.lineTo(4, 14.5);
        Path.closeSubpath();
        Path.moveTo(16, 9);
        Path.arcTo(QRectF(10, 8, 12, 8), 50, -100);
        Path.moveTo(18, 6);
        Path.arcTo(QRectF(8, 4, 16, 16), 40, -80);
        break;
    case Icon::Graphics:
        Rect(3, 5, 18, 12, 2);
        Path.moveTo(9, 20);
        Path.lineTo(15, 20);
        Path.moveTo(12, 17);
        Path.lineTo(12, 20);
        break;
    case Icon::Plugin:
        Path.moveTo(9, 3.5);
        Path.lineTo(9, 8);
        Path.moveTo(15, 3.5);
        Path.lineTo(15, 8);
        Path.moveTo(6, 8);
        Path.lineTo(18, 8);
        Path.lineTo(18, 12.5);
        Path.cubicTo(18, 15.6, 15.6, 17, 12, 17);
        Path.cubicTo(8.4, 17, 6, 15.6, 6, 12.5);
        Path.closeSubpath();
        Path.moveTo(12, 17);
        Path.lineTo(12, 20.5);
        break;
    case Icon::Advanced:
        for (int i = 0; i < 3; i++)
        {
            const qreal Y = 6 + i * 6;
            Path.moveTo(4, Y);
            Path.lineTo(20, Y);
            Path.addEllipse(QPointF(i == 0 ? 15 : i == 1 ? 8 : 13, Y), 2.2, 2.2);
        }
        break;
    case Icon::Cheat:
        Path.moveTo(12, 3);
        Path.lineTo(14.5, 9);
        Path.lineTo(21, 9.5);
        Path.lineTo(16, 13.8);
        Path.lineTo(17.7, 20.5);
        Path.lineTo(12, 16.8);
        Path.lineTo(6.3, 20.5);
        Path.lineTo(8, 13.8);
        Path.lineTo(3, 9.5);
        Path.lineTo(9.5, 9);
        Path.closeSubpath();
        break;
    case Icon::Trash:
        Path.moveTo(4, 7);
        Path.lineTo(20, 7);
        Path.moveTo(9, 7);
        Path.lineTo(9, 4.5);
        Path.lineTo(15, 4.5);
        Path.lineTo(15, 7);
        Path.moveTo(6, 7);
        Path.lineTo(7, 20);
        Path.lineTo(17, 20);
        Path.lineTo(18, 7);
        break;
    case Icon::Keyboard:
        Rect(2.5, 6, 19, 12, 2.5);
        for (int i = 0; i < 4; i++)
        {
            Path.moveTo(6 + i * 4, 10);
            Path.lineTo(6.1 + i * 4, 10);
        }
        Path.moveTo(7, 14.5);
        Path.lineTo(17, 14.5);
        break;
    case Icon::Eject:
        Path.moveTo(5, 14);
        Path.lineTo(12, 5);
        Path.lineTo(19, 14);
        Path.closeSubpath();
        Path.moveTo(5, 18.5);
        Path.lineTo(19, 18.5);
        break;
    }
    StrokePath(P, Path, 1.8);
}
} // namespace

QPixmap IconPixmap(Icon Which, const QColor & Color, int Size, qreal Ratio)
{
    QPixmap Pixmap(QSize(Size, Size) * Ratio);
    Pixmap.setDevicePixelRatio(Ratio);
    Pixmap.fill(Qt::transparent);
    QPainter P(&Pixmap);
    P.setRenderHint(QPainter::Antialiasing);
    P.scale(Size / 24.0, Size / 24.0);
    P.setPen(Color);
    Draw(P, Which);
    return Pixmap;
}

QIcon MakeIcon(Icon Which, const QColor & Color, int Size)
{
    QIcon Result;
    const qreal Ratios[] = {1.0, 2.0};
    for (qreal Ratio : Ratios)
    {
        Result.addPixmap(IconPixmap(Which, Color, Size, Ratio), QIcon::Normal, QIcon::Off);
        Result.addPixmap(IconPixmap(Which, Color, Size, Ratio), QIcon::Normal, QIcon::On);
        Result.addPixmap(IconPixmap(Which, T().TextDisabled, Size, Ratio), QIcon::Disabled, QIcon::Off);
        Result.addPixmap(IconPixmap(Which, T().TextDisabled, Size, Ratio), QIcon::Disabled, QIcon::On);
    }
    return Result;
}

QIcon MakeIcon(Icon Which, int Size)
{
    return MakeIcon(Which, T().TextPrimary, Size);
}

// The official logo (Assets/Vortex64/Vortex.png) as a white glyph with alpha, tinted with the colour asked for
static QImage Glyph()
{
    static const QImage Image = QImage(QStringLiteral(":/vortex/logo-glyph.png")).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    return Image;
}

void PaintLogo(QPainter & P, const QRectF & Rect, const QColor & Color)
{
    P.save();
    P.setRenderHint(QPainter::SmoothPixmapTransform);
    const QSizeF Target = Rect.size();
    const qreal Ratio = P.device()->devicePixelRatioF();
    const QSize Pixels(qMax(1, int(Target.width() * Ratio)), qMax(1, int(Target.height() * Ratio)));
    // (scaled from the 1024 pixel glyph each time it is needed at a new size, never from a smaller copy)
    static QHash<QString, QImage> Cache;
    const QString Key = QStringLiteral("%1x%2/%3").arg(Pixels.width()).arg(Pixels.height()).arg(Color.name(QColor::HexArgb));
    QImage Tinted = Cache.value(Key);
    if (Tinted.isNull())
    {
        Tinted = Glyph().scaled(Pixels, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPainter T(&Tinted);
        T.setCompositionMode(QPainter::CompositionMode_SourceIn);
        T.fillRect(Tinted.rect(), Color);
        T.end();
        if (Cache.size() > 64)
        {
            Cache.clear();
        }
        Cache.insert(Key, Tinted);
    }
    Tinted.setDevicePixelRatio(Ratio);
    const QSizeF Logical = QSizeF(Tinted.size()) / Ratio;
    P.drawImage(QPointF(Rect.center().x() - Logical.width() / 2, Rect.center().y() - Logical.height() / 2), Tinted);
    P.restore();
}

QPixmap LogoPixmap(int Size, const QColor & Color, qreal Ratio)
{
    QPixmap Pixmap(QSize(Size, Size) * Ratio);
    Pixmap.setDevicePixelRatio(Ratio);
    Pixmap.fill(Qt::transparent);
    QPainter P(&Pixmap);
    PaintLogo(P, QRectF(0, 0, Size, Size), Color);
    return Pixmap;
}

QIcon AppIcon()
{
    QIcon Result;
    const int Sizes[] = {16, 24, 32, 48, 64, 128, 256, 512};
    for (int Size : Sizes)
    {
        QPixmap Pixmap(Size, Size);
        Pixmap.fill(Qt::transparent);
        QPainter P(&Pixmap);
        P.setRenderHint(QPainter::Antialiasing);
        P.setPen(Qt::NoPen);
        P.setBrush(QColor(0x1a, 0x1c, 0x20));
        P.drawRoundedRect(QRectF(0, 0, Size, Size), Size * 0.2, Size * 0.2);
        PaintLogo(P, QRectF(Size * 0.11, Size * 0.11, Size * 0.78, Size * 0.78), QColor(0xe8, 0xea, 0xed));
        P.end();
        Result.addPixmap(Pixmap);
    }
    return Result;
}

QString RegionName(char Country)
{
    switch (Country)
    {
    case 'E': case 'A': case '7': return QStringLiteral("USA");
    case 'J': return QStringLiteral("JPN");
    case 'P': case 'X': case 'Y': case 'L': return QStringLiteral("EUR");
    case 'D': return QStringLiteral("GER");
    case 'F': return QStringLiteral("FRA");
    case 'I': return QStringLiteral("ITA");
    case 'S': return QStringLiteral("SPA");
    case 'U': return QStringLiteral("AUS");
    case 'B': return QStringLiteral("BRA");
    case 'H': return QStringLiteral("NED");
    case 'K': return QStringLiteral("KOR");
    case 'C': return QStringLiteral("CHN");
    case 'W': return QStringLiteral("SCA");
    case 'G': return QStringLiteral("GW");
    default: return QStringLiteral("—");
    }
}

QString RegionLong(char Country)
{
    switch (Country)
    {
    case 'E': case 'A': case '7': case 'B': return QStringLiteral("NTSC-U");
    case 'J': case 'K': case 'C': return QStringLiteral("NTSC-J");
    case 'P': case 'X': case 'Y': case 'D': case 'F': case 'I': case 'S': case 'U': case 'H': case 'L': case 'W': return QStringLiteral("PAL");
    default: return QStringLiteral("—");
    }
}

void PaintFlag(QPainter & P, const QRectF & R, char Country)
{
    P.save();
    P.setRenderHint(QPainter::Antialiasing);
    QPainterPath Clip;
    Clip.addRoundedRect(R, 2, 2);
    P.setClipPath(Clip);
    P.setPen(Qt::NoPen);
    const QString Name = RegionName(Country);
    if (Name == "USA")
    {
        P.fillRect(R, QColor(0xf4, 0xf4, 0xf4));
        for (int i = 0; i < 7; i++)
        {
            P.fillRect(QRectF(R.left(), R.top() + R.height() * (i * 2) / 13.0, R.width(), R.height() / 13.0), QColor(0xc8, 0x32, 0x3c));
        }
        P.fillRect(QRectF(R.left(), R.top(), R.width() * 0.45, R.height() * 7 / 13.0), QColor(0x2d, 0x40, 0x8c));
    }
    else if (Name == "JPN")
    {
        P.fillRect(R, QColor(0xf4, 0xf4, 0xf4));
        P.setBrush(QColor(0xc8, 0x28, 0x3c));
        P.drawEllipse(R.center(), R.height() * 0.28, R.height() * 0.28);
    }
    else if (Name == "EUR")
    {
        P.fillRect(R, QColor(0x2d, 0x40, 0x9a));
        P.setBrush(QColor(0xf2, 0xc9, 0x32));
        for (int i = 0; i < 8; i++)
        {
            const qreal A = i * M_PI / 4;
            P.drawEllipse(R.center() + QPointF(qCos(A), qSin(A)) * R.height() * 0.3, R.height() * 0.06, R.height() * 0.06);
        }
    }
    else
    {
        P.fillRect(R, QColor(0x59, 0x60, 0x6c));
        P.setBrush(QColor(0xb5, 0xba, 0xc2));
        P.drawEllipse(R.center(), R.height() * 0.25, R.height() * 0.25);
    }
    P.setClipping(false);
    P.setBrush(Qt::NoBrush);
    P.setPen(QPen(QColor(0, 0, 0, 90), 1));
    P.drawRoundedRect(R.adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
    P.restore();
}
} // namespace Vortex
