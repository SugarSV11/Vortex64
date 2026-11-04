#pragma once

#include "LibraryModel.h"

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QImage>

namespace Vortex
{
// The covers of the library. Only local images are used (nothing is downloaded): a file next to the ROM
// (<rom>.png|jpg|webp, cover.*, folder.*) or in the covers directory of the store
// ($XDG_CONFIG_HOME/vortex64/covers/<name>.png, <GoodName>.png, <CRC1>-<CRC2>.png). They are read and scaled
// in a worker thread and kept in a cache; until an image is ready, and for games without one, the card shows a
// placeholder with the logo and the initials of the game (always the same colour for the same game).
class Covers : public QObject
{
    Q_OBJECT
public:
    static Covers & Instance();

    // A pixmap of Size (logical pixels) for the entry; never null
    QPixmap Cover(const Entry & E, const QSize & Size, qreal DevicePixelRatio);
    // The file with the cover, empty when there is none
    QString FindCoverFile(const Entry & E) const;
    // The placeholder alone (used by the tests and the About screen)
    static QPixmap Placeholder(const QString & Name, const QSize & Size, qreal DevicePixelRatio, const QString & Region = QString());
    void Clear();

public Q_SLOTS:
    void OnLoaded(const QString & Key, const QString & RomPath, const QImage & Image);

Q_SIGNALS:
    void CoverReady(const QString & RomPath);

private:
    Covers() = default;
    void Request(const QString & Key, const QString & File, const QString & RomPath, const QSize & PixelSize);

    QHash<QString, QPixmap> m_Cache;
    QSet<QString> m_Pending;
};
} // namespace Vortex
