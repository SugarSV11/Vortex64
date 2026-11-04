// Builds the assets of the Vortex64 logo from the official artwork (Assets/Vortex64/Vortex.png: grey tornado on white):
//   Vortex-glyph.png        the tornado alone, white with alpha, cropped and square (what the application tints and paints)
//   packaging/linux/icons/  vortex64-<16..512>.png (dark rounded tile with the glyph) and vortex64.svg
//
//   vortex64-icongen <Vortex.png> <glyph output .png> <icons directory>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <cstdio>

int main(int argc, char ** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication App(argc, argv);
    if (argc < 4)
    {
        printf("usage: %s <Vortex.png> <glyph.png> <icons directory>\n", argv[0]);
        return 2;
    }
    QImage Source(QString::fromLocal8Bit(argv[1]));
    if (Source.isNull())
    {
        printf("cannot read %s\n", argv[1]);
        return 1;
    }
    Source = Source.convertToFormat(QImage::Format_RGB32);
    // the artwork is mid grey (about 128) on white: the darkness is the coverage
    QImage Glyph(Source.size(), QImage::Format_ARGB32);
    int MinX = Source.width(), MinY = Source.height(), MaxX = 0, MaxY = 0;
    for (int Y = 0; Y < Source.height(); Y++)
    {
        for (int X = 0; X < Source.width(); X++)
        {
            const int L = qGray(Source.pixel(X, Y));
            const int A = qBound(0, ((255 - L) - 14) * 255 / (127 - 14), 255); // (the white of the artwork is not pure: the floor is noise)
            Glyph.setPixel(X, Y, qRgba(255, 255, 255, A));
            if (A > 24)
            {
                MinX = qMin(MinX, X);
                MaxX = qMax(MaxX, X);
                MinY = qMin(MinY, Y);
                MaxY = qMax(MaxY, Y);
            }
        }
    }
    const int Side = qMax(MaxX - MinX, MaxY - MinY) + 1;
    const int Pad = Side / 16;
    QImage Square(Side + 2 * Pad, Side + 2 * Pad, QImage::Format_ARGB32);
    Square.fill(Qt::transparent);
    {
        QPainter P(&Square);
        const int OffsetX = Pad + (Side - (MaxX - MinX + 1)) / 2, OffsetY = Pad + (Side - (MaxY - MinY + 1)) / 2;
        P.drawImage(OffsetX, OffsetY, Glyph, MinX, MinY, MaxX - MinX + 1, MaxY - MinY + 1);
    }
    const QImage Final = Square.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QDir().mkpath(QFileInfo(QString::fromLocal8Bit(argv[2])).absolutePath());
    if (!Final.save(QString::fromLocal8Bit(argv[2])))
    {
        return 1;
    }

    const QString Dir = QString::fromLocal8Bit(argv[3]);
    QDir().mkpath(Dir);
    auto Tile = [&](int Size) {
        // every size comes from the 1024 pixel glyph, never from a smaller icon
        QImage Image(Size, Size, QImage::Format_ARGB32);
        Image.fill(Qt::transparent);
        QPainter P(&Image);
        P.setRenderHint(QPainter::Antialiasing);
        P.setRenderHint(QPainter::SmoothPixmapTransform);
        P.setPen(Qt::NoPen);
        P.setBrush(QColor(0x1a, 0x1c, 0x20));
        P.drawRoundedRect(QRectF(0, 0, Size, Size), Size * 0.2, Size * 0.2);
        // tint: the glyph is white; paint it with the colour of the theme
        QImage Tinted = Final.scaled(int(Size * 0.78), int(Size * 0.78), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPainter T(&Tinted);
        T.setCompositionMode(QPainter::CompositionMode_SourceIn);
        T.fillRect(Tinted.rect(), QColor(0xe8, 0xea, 0xed));
        T.end();
        P.drawImage((Size - Tinted.width()) / 2, (Size - Tinted.height()) / 2, Tinted);
        return Image;
    };
    for (int Size : {16, 24, 32, 48, 64, 128, 256, 512})
    {
        if (!Tile(Size).save(Dir + QString("/vortex64-%1.png").arg(Size)))
        {
            return 1;
        }
    }
    QByteArray Png;
    QBuffer Buffer(&Png);
    Buffer.open(QIODevice::WriteOnly);
    Tile(512).save(&Buffer, "PNG");
    QFile Svg(Dir + "/vortex64.svg");
    if (!Svg.open(QIODevice::WriteOnly))
    {
        return 1;
    }
    Svg.write("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" viewBox=\"0 0 512 512\" width=\"512\" height=\"512\">\n<image width=\"512\" height=\"512\" xlink:href=\"data:image/png;base64,");
    Svg.write(Png.toBase64());
    Svg.write("\"/>\n</svg>\n");
    printf("assets written\n");
    return 0;
}
