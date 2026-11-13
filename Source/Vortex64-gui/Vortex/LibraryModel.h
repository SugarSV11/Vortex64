#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QSortFilterProxyModel>
#include <QVector>

class QtRomBrowser;

namespace Vortex
{
// A game (or a 64DD disk) of the library, taken from what the core's CRomList knows about the file
struct Entry
{
    QString Path;       // szFullFileName (the file to run; "archive.7z?inner" for 7z)
    QString Name;       // the name to show
    QString GoodName;
    QString InternalName;
    QString FileName;
    QString Status;
    QString Developer;
    QString Genre;
    QString Notes;
    char Country = 0;
    quint8 Media = 0;
    qint64 Size = 0;    // bytes
    QString Format;     // ".z64"
    uint Crc1 = 0, Crc2 = 0;
    int Players = 0;
    bool IsDisk = false;
};

class LibraryModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role
    {
        PathRole = Qt::UserRole + 1,
        NameRole,
        RegionRole,
        RegionLongRole,
        SizeRole,
        FormatRole,
        FavoriteRole,
        LastPlayedRole,
        PlaySecondsRole,
        IsDiskRole,
        CountryRole,
        EntryIndexRole,
        SearchRole,
    };

    explicit LibraryModel(QObject * Parent = nullptr);
    // The scanner whose list this presents (it keeps scanning, caching and watching the directories)
    void SetSource(QtRomBrowser * Source);
    QtRomBrowser * Source() const
    {
        return m_Source;
    }

    // The columns of the list views
    enum Column
    {
        ColName,
        ColRegion,
        ColSize,
        ColFormat,
        ColLast,
        ColPlayed,
        ColFavorite,
        ColumnCount
    };
    static int ColumnForKey(const QString & Key);
    static QString HeaderText(int Column);
    static QString PlayTimeText(qint64 Seconds);
    static QString DateText(const QDateTime & Time);

    int rowCount(const QModelIndex & Parent = QModelIndex()) const override;
    int columnCount(const QModelIndex & Parent = QModelIndex()) const override;
    QVariant headerData(int Section, Qt::Orientation Orientation, int Role) const override;
    QVariant data(const QModelIndex & Index, int Role) const override;

    const Entry * Find(const QString & Path) const;
    const Entry & At(int Row) const
    {
        return m_Entries[Row];
    }
    int Count() const
    {
        return m_Entries.size();
    }
    bool Scanning() const
    {
        return m_Scanning;
    }
    int ScannedSoFar() const
    {
        return m_ScannedSoFar;
    }
    static QString DisplayName(const Entry & E);
    static QString SizeText(qint64 Bytes);

Q_SIGNALS:
    void ScanStateChanged(bool Scanning, int Count);

private:
    void Reload();
    QtRomBrowser * m_Source = nullptr;
    QVector<Entry> m_Entries;
    QHash<QString, int> m_ByPath;
    bool m_Scanning = false;
    int m_ScannedSoFar = 0;
};

// A view of the model: what a page of the sidebar shows
class LibraryProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    enum Scope
    {
        Games,      // the cartridges
        Favorites,  // marked with the star (games and disks)
        Recents,    // played at least once
        Disks,      // 64DD disks
    };
    explicit LibraryProxy(Scope S, QObject * Parent = nullptr);

    void SetText(const QString & Text);
    // "name", "region", "size", "recent", "played"
    void SetSortKey(const QString & Key, bool Ascending);
    Scope ScopeOf() const
    {
        return m_Scope;
    }

protected:
    bool filterAcceptsRow(int Row, const QModelIndex & Parent) const override;
    bool lessThan(const QModelIndex & Left, const QModelIndex & Right) const override;

private:
    Scope m_Scope;
    QStringList m_Words;
};
} // namespace Vortex
