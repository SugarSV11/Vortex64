#include "LibraryModel.h"
#include "../RomBrowser.h"
#include "Icons.h"
#include "Store.h"
#include "Strings.h"

#include <QFileInfo>
#include <QStringDecoder>

namespace Vortex
{
namespace
{
QString FromRom(const char * Text)
{
    return QString::fromUtf8(Text);
}
} // namespace

LibraryModel::LibraryModel(QObject * Parent) :
    QAbstractListModel(Parent)
{
    connect(&Store::Instance(), &Store::FavoritesChanged, this, [this]() {
        if (!m_Entries.isEmpty())
        {
            Q_EMIT dataChanged(index(0), index(m_Entries.size() - 1), {FavoriteRole});
        }
    });
    connect(&Store::Instance(), &Store::HistoryChanged, this, [this]() {
        if (!m_Entries.isEmpty())
        {
            Q_EMIT dataChanged(index(0), index(m_Entries.size() - 1), {LastPlayedRole, PlaySecondsRole});
        }
    });
}

void LibraryModel::SetSource(QtRomBrowser * Source)
{
    m_Source = Source;
    connect(Source, &QtRomBrowser::ListReset, this, [this]() {
        m_Scanning = true;
        m_ScannedSoFar = 0;
        Q_EMIT ScanStateChanged(true, 0);
    });
    connect(Source, &QtRomBrowser::RomCountChanged, this, [this](int Count) {
        m_ScannedSoFar = Count;
        if (Count % 8 == 0)
        {
            Q_EMIT ScanStateChanged(true, Count);
        }
    });
    connect(Source, &QtRomBrowser::ListLoaded, this, [this]() {
        Reload();
        m_Scanning = false;
        Q_EMIT ScanStateChanged(false, m_Entries.size());
    });
    Reload();
}

QString LibraryModel::SizeText(qint64 Bytes)
{
    const double MB = Bytes / 1048576.0;
    if (MB >= 1000)
    {
        return QString::asprintf("%.1f GB", MB / 1024.0);
    }
    return MB == (qint64)MB ? QString::asprintf("%lld MB", (long long)MB) : QString::asprintf("%.1f MB", MB);
}

void LibraryModel::Reload()
{
    beginResetModel();
    m_Entries.clear();
    m_ByPath.clear();
    if (m_Source != nullptr)
    {
        const int Count = m_Source->RomCount();
        m_Entries.reserve(Count);
        for (int i = 0; i < Count; i++)
        {
            const auto & Info = m_Source->RomAt(i);
            Entry E;
            E.Path = QString::fromLocal8Bit(Info.szFullFileName);
            E.FileName = QString::fromLocal8Bit(Info.FileName);
            E.GoodName = FromRom(Info.GoodName);
            if (E.GoodName.startsWith('#'))
            {
                E.GoodName.clear();
            }
            E.InternalName = QString::fromLatin1(Info.InternalName).trimmed();
            QStringDecoder Decoder("Shift-JIS");
            if (Decoder.isValid())
            {
                const QString Decoded = QString(Decoder(QByteArray(Info.InternalName))).trimmed();
                if (!Decoded.isEmpty())
                {
                    E.InternalName = Decoded;
                }
            }
            QString Name = FromRom(Info.Name);
            if (Name.startsWith('#'))
            {
                Name.clear();
            }
            E.Name = Name;
            E.Status = FromRom(Info.Status);
            E.Developer = FromRom(Info.Developer);
            E.Genre = FromRom(Info.Genre);
            E.Notes = FromRom(Info.UserNotes);
            E.Country = (char)Info.Country;
            E.Media = Info.Media;
            E.Size = Info.RomSize;
            E.Crc1 = Info.CRC1;
            E.Crc2 = Info.CRC2;
            E.Players = Info.Players;
            const QString Inner = E.Path.contains('?') ? E.Path.section('?', 1) : E.Path;
            const QString Suffix = QFileInfo(Inner).suffix().toLower();
            E.Format = Suffix.isEmpty() ? QString() : QStringLiteral(".") + Suffix;
            if (Info.FileFormat == CRomList::Format_Zip)
            {
                E.Format += QStringLiteral(" (zip)");
            }
            else if (Info.FileFormat == CRomList::Format_7zip)
            {
                E.Format += QStringLiteral(" (7z)");
            }
            E.IsDisk = Info.Media == 'D' || Info.Media == 'E' || Suffix == QLatin1String("ndd") || Suffix == QLatin1String("d64");
            m_ByPath.insert(E.Path, m_Entries.size());
            m_Entries.push_back(E);
        }
    }
    endResetModel();
}

const Entry * LibraryModel::Find(const QString & Path) const
{
    const auto It = m_ByPath.constFind(Path);
    return It == m_ByPath.constEnd() ? nullptr : &m_Entries[It.value()];
}

QString LibraryModel::DisplayName(const Entry & E)
{
    if (!E.Name.isEmpty())
    {
        return E.Name;
    }
    // (a game the database does not know has no name: its file name is the best there is)
    const QString Base = QFileInfo(E.FileName).completeBaseName();
    if (!Base.isEmpty())
    {
        return Base;
    }
    return E.InternalName.isEmpty() ? E.GoodName : E.InternalName;
}

int LibraryModel::rowCount(const QModelIndex & Parent) const
{
    return Parent.isValid() ? 0 : m_Entries.size();
}

int LibraryModel::columnCount(const QModelIndex & Parent) const
{
    return Parent.isValid() ? 0 : ColumnCount;
}

int LibraryModel::ColumnForKey(const QString & Key)
{
    if (Key == QLatin1String("region"))
    {
        return ColRegion;
    }
    if (Key == QLatin1String("size"))
    {
        return ColSize;
    }
    if (Key == QLatin1String("recent"))
    {
        return ColLast;
    }
    if (Key == QLatin1String("played"))
    {
        return ColPlayed;
    }
    return ColName;
}

QString LibraryModel::HeaderText(int Column)
{
    switch (Column)
    {
    case ColName: return Tr("Nombre", "Name");
    case ColRegion: return Tr("Región", "Region");
    case ColSize: return Tr("Tamaño", "Size");
    case ColFormat: return Tr("Formato", "Format");
    case ColLast: return Tr("Última vez", "Last played");
    case ColPlayed: return Tr("Tiempo jugado", "Play time");
    default: return QString();
    }
}

QVariant LibraryModel::headerData(int Section, Qt::Orientation Orientation, int Role) const
{
    if (Orientation == Qt::Horizontal && Role == Qt::DisplayRole)
    {
        return HeaderText(Section);
    }
    return QAbstractListModel::headerData(Section, Orientation, Role);
}

// "2 h 15 min", "45 min", "less than a minute"
QString LibraryModel::PlayTimeText(qint64 Seconds)
{
    if (Seconds < 60)
    {
        return Seconds <= 0 ? QStringLiteral("—") : QStringLiteral("< 1 min");
    }
    const qint64 Minutes = Seconds / 60;
    if (Minutes < 60)
    {
        return QStringLiteral("%1 min").arg(Minutes);
    }
    return QStringLiteral("%1 h %2 min").arg(Minutes / 60).arg(Minutes % 60);
}

QString LibraryModel::DateText(const QDateTime & Time)
{
    if (!Time.isValid())
    {
        return QStringLiteral("—");
    }
    return QLocale(CurrentLanguage() == Lang::Spanish ? QLocale::Spanish : QLocale::English).toString(Time, CurrentLanguage() == Lang::Spanish ? "d MMM. yyyy  HH:mm" : "MMM d, yyyy  HH:mm");
}

QVariant LibraryModel::data(const QModelIndex & Index, int Role) const
{
    if (!Index.isValid() || Index.row() >= m_Entries.size())
    {
        return QVariant();
    }
    const Entry & E = m_Entries[Index.row()];
    if (Role == Qt::DisplayRole)
    {
        switch (Index.column())
        {
        case ColName: return DisplayName(E);
        case ColRegion: return RegionName(E.Country);
        case ColSize: return SizeText(E.Size);
        case ColFormat: return E.Format;
        case ColLast: return DateText(Store::Instance().Record(E.Path).LastPlayed);
        case ColPlayed: return PlayTimeText(Store::Instance().Record(E.Path).Seconds);
        default: return QVariant();
        }
    }
    switch (Role)
    {
    case NameRole: return DisplayName(E);
    case PathRole: return E.Path;
    case RegionRole: return RegionName(E.Country);
    case RegionLongRole: return RegionLong(E.Country);
    case SizeRole: return E.Size;
    case FormatRole: return E.Format;
    case FavoriteRole: return Store::Instance().IsFavorite(E.Path);
    case LastPlayedRole: return Store::Instance().Record(E.Path).LastPlayed;
    case PlaySecondsRole: return Store::Instance().Record(E.Path).Seconds;
    case IsDiskRole: return E.IsDisk;
    case CountryRole: return (int)E.Country;
    case EntryIndexRole: return Index.row();
    case SearchRole: return DisplayName(E) + QLatin1Char('\n') + E.GoodName + QLatin1Char('\n') + E.InternalName + QLatin1Char('\n') + E.FileName;
    default: return QVariant();
    }
}

LibraryProxy::LibraryProxy(Scope S, QObject * Parent) :
    QSortFilterProxyModel(Parent),
    m_Scope(S)
{
    setDynamicSortFilter(true);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setSortLocaleAware(true);
    if (S == Recents)
    {
        sort(LibraryModel::ColLast, Qt::DescendingOrder);
    }
}

void LibraryProxy::SetText(const QString & Text)
{
    m_Words = Text.toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    invalidateFilter();
}

void LibraryProxy::SetSortKey(const QString & Key, bool Ascending)
{
    sort(LibraryModel::ColumnForKey(Key), Ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
}

bool LibraryProxy::filterAcceptsRow(int Row, const QModelIndex & Parent) const
{
    const QModelIndex Index = sourceModel()->index(Row, 0, Parent);
    const bool IsDisk = Index.data(LibraryModel::IsDiskRole).toBool();
    switch (m_Scope)
    {
    case Games:
        if (IsDisk)
        {
            return false;
        }
        break;
    case Disks:
        if (!IsDisk)
        {
            return false;
        }
        break;
    case Favorites:
        if (!Index.data(LibraryModel::FavoriteRole).toBool())
        {
            return false;
        }
        break;
    case Recents:
        if (!Index.data(LibraryModel::LastPlayedRole).toDateTime().isValid())
        {
            return false;
        }
        break;
    }
    if (!m_Words.isEmpty())
    {
        const QString Haystack = Index.data(LibraryModel::SearchRole).toString().toLower();
        for (const QString & Word : m_Words)
        {
            if (!Haystack.contains(Word))
            {
                return false;
            }
        }
    }
    return true;
}

bool LibraryProxy::lessThan(const QModelIndex & L, const QModelIndex & R) const
{
    auto Text = [](const QModelIndex & I) { return I.data(LibraryModel::NameRole).toString(); };
    auto ByName = [&]() { return QString::localeAwareCompare(Text(L), Text(R)) < 0; };
    switch (L.column())
    {
    case LibraryModel::ColRegion:
    {
        const QString A = L.data(LibraryModel::RegionRole).toString(), B = R.data(LibraryModel::RegionRole).toString();
        return A != B ? A < B : ByName();
    }
    case LibraryModel::ColSize:
    {
        const qint64 A = L.data(LibraryModel::SizeRole).toLongLong(), B = R.data(LibraryModel::SizeRole).toLongLong();
        return A != B ? A < B : ByName();
    }
    case LibraryModel::ColLast:
    {
        const QDateTime A = L.data(LibraryModel::LastPlayedRole).toDateTime(), B = R.data(LibraryModel::LastPlayedRole).toDateTime();
        return A != B ? A < B : ByName();
    }
    case LibraryModel::ColPlayed:
    {
        const qint64 A = L.data(LibraryModel::PlaySecondsRole).toLongLong(), B = R.data(LibraryModel::PlaySecondsRole).toLongLong();
        return A != B ? A < B : ByName();
    }
    case LibraryModel::ColFavorite:
    {
        const bool A = L.data(LibraryModel::FavoriteRole).toBool(), B = R.data(LibraryModel::FavoriteRole).toBool();
        return A != B ? A < B : ByName();
    }
    default: return ByName();
    }
}
} // namespace Vortex
