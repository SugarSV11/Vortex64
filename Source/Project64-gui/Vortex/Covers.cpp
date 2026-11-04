#include "Covers.h"
#include "Icons.h"
#include "Store.h"
#include "Theme.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QRunnable>
#include <QThreadPool>
#include <QPointer>
#include <QRegularExpression>

namespace Vortex
{
Covers & Covers::Instance()
{
    static Covers * Instance = new Covers;
    return *Instance;
}

namespace
{
QString FileSafe(QString Name)
{
    Name.replace(QRegularExpression("[/\\\\:*?\"<>|]"), "_");
    return Name;
}
} // namespace

QString Covers::FindCoverFile(const Entry & E) const
{
    static const char * const Extensions[] = {"png", "jpg", "jpeg", "webp"};
    auto Look = [](const QString & Dir, const QString & Base) -> QString {
        for (const char * Ext : Extensions)
        {
            const QString File = Dir + QLatin1Char('/') + Base + QLatin1Char('.') + QLatin1String(Ext);
            if (QFileInfo::exists(File))
            {
                return File;
            }
        }
        return QString();
    };
    const QString RomFile = E.Path.section('?', 0, 0);
    const QFileInfo Info(RomFile);
    QString Found = Look(Info.absolutePath(), Info.completeBaseName());
    if (Found.isEmpty())
    {
        const QString CoverDir = Store::Instance().CoverDirectory();
        const QString Crc = QStringLiteral("%1-%2").arg(E.Crc1, 8, 16, QLatin1Char('0')).arg(E.Crc2, 8, 16, QLatin1Char('0')).toUpper();
        Found = Look(CoverDir, Crc);
        if (Found.isEmpty() && !E.GoodName.isEmpty())
        {
            Found = Look(CoverDir, FileSafe(E.GoodName));
        }
        if (Found.isEmpty() && !E.Name.isEmpty())
        {
            Found = Look(CoverDir, FileSafe(E.Name));
        }
        if (Found.isEmpty())
        {
            Found = Look(CoverDir, Info.completeBaseName());
        }
    }
    if (Found.isEmpty())
    {
        Found = Look(Info.absolutePath(), QStringLiteral("cover"));
    }
    return Found;
}

namespace
{
class LoadTask : public QRunnable
{
public:
    LoadTask(QObject * Target, QString Key, QString File, QString Rom, QSize PixelSize) :
        m_Target(Target), m_Key(std::move(Key)), m_File(std::move(File)), m_Rom(std::move(Rom)), m_Size(PixelSize)
    {
    }
    void run() override
    {
        QImageReader Reader(m_File);
        Reader.setAutoTransform(true);
        const QSize Original = Reader.size();
        if (Original.isValid() && (Original.width() > m_Size.width() * 2 || Original.height() > m_Size.height() * 2))
        {
            Reader.setScaledSize(Original.scaled(m_Size * 2, Qt::KeepAspectRatio));
        }
        QImage Image = Reader.read();
        if (!Image.isNull())
        {
            Image = Image.scaled(m_Size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        QMetaObject::invokeMethod(m_Target, "OnLoaded", Qt::QueuedConnection, Q_ARG(QString, m_Key), Q_ARG(QString, m_Rom), Q_ARG(QImage, Image));
    }

private:
    QPointer<QObject> m_Target;
    QString m_Key, m_File, m_Rom;
    QSize m_Size;
};
} // namespace

QPixmap Covers::Placeholder(const QString & Name, const QSize & Size, qreal Ratio, const QString & Region)
{
    QPixmap Pixmap(Size * Ratio);
    Pixmap.setDevicePixelRatio(Ratio);
    Pixmap.fill(Qt::transparent);
    QPainter P(&Pixmap);
    P.setRenderHint(QPainter::Antialiasing);
    const QRectF R(QPointF(0, 0), QSizeF(Size));
    const Tokens & K = T();
    // the colours of the theme with a touch of a hue that belongs to the game (always the same one)
    const int Hue = qHash(Name) % 360;
    const QColor Tint = QColor::fromHsv(Hue, 90, 150);
    auto Mix = [&](const QColor & Base, qreal Amount) {
        return QColor(int(Base.red() + (Tint.red() - Base.red()) * Amount), int(Base.green() + (Tint.green() - Base.green()) * Amount), int(Base.blue() + (Tint.blue() - Base.blue()) * Amount));
    };
    const qreal Radius = qMin(R.width(), R.height()) >= 60 ? 8 : 5;
    QPainterPath Clip;
    Clip.addRoundedRect(R, Radius, Radius);
    P.setClipPath(Clip);
    QLinearGradient Gradient(R.topLeft(), R.bottomRight());
    Gradient.setColorAt(0, Mix(K.SurfaceElevated.lighter(125), 0.22));
    Gradient.setColorAt(1, Mix(K.Surface.darker(115), 0.14));
    P.fillRect(R, Gradient);
    const bool Big = R.height() >= 90;
    // the tornado, large and quiet, in the upper part
    const qreal LogoSize = qMin(R.width(), R.height()) * (Big ? 0.42 : 0.62);
    PaintLogo(P, QRectF(R.center().x() - LogoSize / 2, R.top() + R.height() * (Big ? 0.08 : 0.19), LogoSize, LogoSize), QColor(255, 255, 255, Big ? 72 : 130));
    if (Big)
    {
        QFont Font;
        Font.setBold(true);
        Font.setPixelSize(qBound(10, int(R.height() * 0.085), 16));
        P.setFont(Font);
        P.setPen(K.TextPrimary);
        QString Shown = Name;
        const int Paren = Shown.lastIndexOf(QStringLiteral(" ("));
        if (Paren > 0 && Shown.endsWith(QLatin1Char(')')))
        {
            Shown = Shown.left(Paren);
        }
        const QRectF TextRect(R.left() + R.width() * 0.09, R.top() + R.height() * 0.5, R.width() * 0.82, R.height() * 0.3);
        P.save();
        P.setClipRect(TextRect);
        P.drawText(TextRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, Shown);
        P.restore();
        if (!Region.isEmpty())
        {
            QFont Small = Font;
            Small.setBold(false);
            Small.setPixelSize(qBound(9, int(R.height() * 0.07), 12));
            P.setFont(Small);
            const QRectF Chip(R.center().x() - 22, R.bottom() - R.height() * 0.08 - 18, 44, 18);
            P.setPen(Qt::NoPen);
            P.setBrush(QColor(0, 0, 0, 90));
            P.drawRoundedRect(Chip, 9, 9);
            P.setPen(K.TextSecondary);
            P.drawText(Chip, Qt::AlignCenter, Region);
        }
    }
    P.setClipping(false);
    P.setPen(QPen(QColor(255, 255, 255, 34), 1));
    P.setBrush(Qt::NoBrush);
    P.drawRoundedRect(R.adjusted(0.5, 0.5, -0.5, -0.5), Radius, Radius);
    return Pixmap;
}

QPixmap Covers::Cover(const Entry & E, const QSize & Size, qreal Ratio)
{
    const QString Key = QStringLiteral("%1|%2x%3@%4").arg(E.Path).arg(Size.width()).arg(Size.height()).arg(Ratio);
    auto Hit = m_Cache.constFind(Key);
    if (Hit != m_Cache.constEnd())
    {
        return Hit.value();
    }
    const QString PlaceholderKey = QStringLiteral("ph|%1|%2x%3@%4|%5").arg(LibraryModel::DisplayName(E)).arg(Size.width()).arg(Size.height()).arg(Ratio).arg(RegionName(E.Country));
    QPixmap Fallback;
    auto PH = m_Cache.constFind(PlaceholderKey);
    if (PH == m_Cache.constEnd())
    {
        Fallback = Placeholder(LibraryModel::DisplayName(E), Size, Ratio, RegionName(E.Country) == QStringLiteral("—") ? QString() : RegionName(E.Country));
        if (m_Cache.size() > 600)
        {
            m_Cache.clear();
        }
        m_Cache.insert(PlaceholderKey, Fallback);
    }
    else
    {
        Fallback = PH.value();
    }
    if (m_Pending.contains(Key))
    {
        return Fallback;
    }
    const QString File = FindCoverFile(E);
    if (File.isEmpty())
    {
        m_Cache.insert(Key, Fallback);
        return Fallback;
    }
    m_Pending.insert(Key);
    Request(Key, File, E.Path, Size * Ratio);
    return Fallback;
}

void Covers::Request(const QString & Key, const QString & File, const QString & RomPath, const QSize & PixelSize)
{
    QThreadPool::globalInstance()->start(new LoadTask(this, Key, File, RomPath, PixelSize));
}

void Covers::Clear()
{
    m_Cache.clear();
    m_Pending.clear();
}

void Covers::OnLoaded(const QString & Key, const QString & RomPath, const QImage & Image)
{
    m_Pending.remove(Key);
    if (Image.isNull())
    {
        return;
    }
    // Key ends with "@<ratio>": the image was scaled to the pixel size, the pixmap needs the ratio back
    const qreal Ratio = Key.section('@', -1).toDouble();
    QPixmap Pixmap = QPixmap::fromImage(Image);
    Pixmap.setDevicePixelRatio(Ratio > 0 ? Ratio : 1.0);
    // rounded corners like the placeholder
    QPixmap Rounded(Pixmap.size());
    Rounded.setDevicePixelRatio(Pixmap.devicePixelRatio());
    Rounded.fill(Qt::transparent);
    QPainter P(&Rounded);
    P.setRenderHint(QPainter::Antialiasing);
    QPainterPath Clip;
    Clip.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(Pixmap.size()) / Pixmap.devicePixelRatio()), 8, 8);
    P.setClipPath(Clip);
    P.drawPixmap(0, 0, Pixmap);
    m_Cache.insert(Key, Rounded);
    Q_EMIT CoverReady(RomPath);
}
}
