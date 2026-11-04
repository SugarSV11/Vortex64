#include "Store.h"
#include "Strings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

namespace Vortex
{
static QString g_OverridePath;
static Lang g_Language = Lang::English;

static bool g_LanguageKnown = false;

Lang CurrentLanguage()
{
    if (!g_LanguageKnown)
    {
        // (the first text asked for: the store knows the language of the user)
        g_LanguageKnown = true;
        Store::Instance();
    }
    return g_Language;
}

static void PublishLanguage()
{
    // (the plugins' configuration windows are built in other libraries: they read the language from here)
    if (qApp != nullptr)
    {
        qApp->setProperty("vortexLanguage", g_Language == Lang::Spanish ? "es" : "en");
    }
}

void SetLanguage(const QString & Code)
{
    g_LanguageKnown = true;
    if (Code == QLatin1String("es"))
    {
        g_Language = Lang::Spanish;
    }
    else if (Code == QLatin1String("en"))
    {
        g_Language = Lang::English;
    }
    else
    {
        g_Language = QLocale::system().language() == QLocale::Spanish ? Lang::Spanish : Lang::English;
    }
    PublishLanguage();
}

QString Store::DefaultPath()
{
    if (!g_OverridePath.isEmpty())
    {
        return g_OverridePath;
    }
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/vortex64/vortex.json");
}

void Store::SetPath(const QString & Path)
{
    g_OverridePath = Path;
}

Store & Store::Instance()
{
    static Store * Instance = new Store;
    return *Instance;
}

Store::Store()
{
    m_Path = DefaultPath();
    m_SaveTimer.setSingleShot(true);
    m_SaveTimer.setInterval(400);
    connect(&m_SaveTimer, &QTimer::timeout, this, &Store::Flush);
    Load();
    Vortex::SetLanguage(Language());
}

void Store::Load()
{
    QFile File(m_Path);
    if (!File.open(QIODevice::ReadOnly))
    {
        return;
    }
    const QJsonDocument Doc = QJsonDocument::fromJson(File.readAll());
    if (!Doc.isObject())
    {
        return;
    }
    m_Json = Doc.object();
    for (const QJsonValue & Value : m_Json.value("favorites").toArray())
    {
        m_Favorites.insert(Value.toString());
    }
    const QJsonObject Plays = m_Json.value("plays").toObject();
    for (auto It = Plays.begin(); It != Plays.end(); ++It)
    {
        const QJsonObject O = It.value().toObject();
        PlayRecord Record;
        Record.LastPlayed = QDateTime::fromString(O.value("last").toString(), Qt::ISODate);
        Record.Seconds = (qint64)O.value("seconds").toDouble();
        Record.Launches = O.value("launches").toInt();
        m_Plays.insert(It.key(), Record);
    }
}

void Store::Touch()
{
    m_Dirty = true;
    m_SaveTimer.start();
}

void Store::Flush()
{
    if (!m_Dirty)
    {
        return;
    }
    QJsonArray Favs;
    QStringList Sorted = m_Favorites.values();
    std::sort(Sorted.begin(), Sorted.end());
    for (const QString & Fav : Sorted)
    {
        Favs.append(Fav);
    }
    QJsonObject Plays;
    for (auto It = m_Plays.begin(); It != m_Plays.end(); ++It)
    {
        QJsonObject O;
        O.insert("last", It.value().LastPlayed.toString(Qt::ISODate));
        O.insert("seconds", (double)It.value().Seconds);
        O.insert("launches", It.value().Launches);
        Plays.insert(It.key(), O);
    }
    m_Json.insert("version", 1);
    m_Json.insert("favorites", Favs);
    m_Json.insert("plays", Plays);
    QDir().mkpath(QFileInfo(m_Path).absolutePath());
    QSaveFile File(m_Path);
    if (File.open(QIODevice::WriteOnly))
    {
        File.write(QJsonDocument(m_Json).toJson(QJsonDocument::Indented));
        if (File.commit())
        {
            m_Dirty = false;
        }
    }
}

// ---- favourites

bool Store::IsFavorite(const QString & Rom) const
{
    return m_Favorites.contains(Rom);
}

void Store::SetFavorite(const QString & Rom, bool On)
{
    if (On == m_Favorites.contains(Rom))
    {
        return;
    }
    if (On)
    {
        m_Favorites.insert(Rom);
    }
    else
    {
        m_Favorites.remove(Rom);
    }
    Touch();
    Q_EMIT FavoritesChanged();
}

QStringList Store::Favorites() const
{
    return m_Favorites.values();
}

// ---- history

PlayRecord Store::Record(const QString & Rom) const
{
    return m_Plays.value(Rom);
}

QStringList Store::RecentRoms() const
{
    QList<QPair<QDateTime, QString>> List;
    for (auto It = m_Plays.begin(); It != m_Plays.end(); ++It)
    {
        if (It.value().LastPlayed.isValid())
        {
            List.append({It.value().LastPlayed, It.key()});
        }
    }
    std::sort(List.begin(), List.end(), [](const auto & A, const auto & B) { return A.first > B.first; });
    QStringList Result;
    for (const auto & Item : List)
    {
        Result << Item.second;
    }
    return Result;
}

void Store::ForgetRecent(const QString & Rom)
{
    auto It = m_Plays.find(Rom);
    if (It != m_Plays.end())
    {
        It->LastPlayed = QDateTime();
        Touch();
        Q_EMIT HistoryChanged();
    }
}

void Store::SessionStart(const QString & Rom)
{
    SessionEnd();
    m_SessionRom = Rom;
    m_SessionStarted = QDateTime::currentDateTime();
    m_SessionAccumulated = 0;
    m_SessionPausedNow = false;
    PlayRecord & Record = m_Plays[Rom];
    Record.LastPlayed = m_SessionStarted;
    Record.Launches++;
    Touch();
    Q_EMIT HistoryChanged();
}

qint64 Store::SessionSeconds() const
{
    if (m_SessionRom.isEmpty())
    {
        return 0;
    }
    qint64 Total = m_SessionAccumulated;
    if (!m_SessionPausedNow)
    {
        Total += m_SessionStarted.secsTo(QDateTime::currentDateTime());
    }
    return Total;
}

void Store::SessionPaused(bool Paused)
{
    if (m_SessionRom.isEmpty() || Paused == m_SessionPausedNow)
    {
        return;
    }
    const QDateTime Now = QDateTime::currentDateTime();
    if (Paused)
    {
        m_SessionAccumulated += m_SessionStarted.secsTo(Now);
    }
    else
    {
        m_SessionStarted = Now;
    }
    m_SessionPausedNow = Paused;
}

void Store::SessionEnd()
{
    if (m_SessionRom.isEmpty())
    {
        return;
    }
    const qint64 Seconds = SessionSeconds();
    PlayRecord & Record = m_Plays[m_SessionRom];
    Record.Seconds += Seconds;
    m_SessionRom.clear();
    m_SessionAccumulated = 0;
    Touch();
    Flush();
    Q_EMIT HistoryChanged();
}

// ---- directories

QStringList Store::ExtraDirectories() const
{
    QStringList List;
    for (const QJsonValue & Value : m_Json.value("directories").toArray())
    {
        List << Value.toString();
    }
    return List;
}

void Store::SetExtraDirectories(const QStringList & Directories)
{
    QJsonArray Array;
    for (const QString & Dir : Directories)
    {
        Array.append(Dir);
    }
    m_Json.insert("directories", Array);
    Touch();
    Q_EMIT DirectoriesChanged();
}

// ---- interface state

QString Store::ViewMode() const
{
    return m_Json.value("view").toString("grid");
}
void Store::SetViewMode(const QString & Mode)
{
    m_Json.insert("view", Mode);
    Touch();
    Q_EMIT ViewChanged();
}
QString Store::SortKey() const
{
    return m_Json.value("sortKey").toString("name");
}
bool Store::SortAscending() const
{
    return m_Json.value("sortAscending").toBool(true);
}
void Store::SetSort(const QString & Key, bool Ascending)
{
    m_Json.insert("sortKey", Key);
    m_Json.insert("sortAscending", Ascending);
    Touch();
    Q_EMIT ViewChanged();
}
QString Store::Filter() const
{
    return m_Json.value("filter").toString();
}
void Store::SetFilter(const QString & Text)
{
    m_Json.insert("filter", Text);
    Touch();
}
QString Store::LastSelected() const
{
    return m_Json.value("lastSelected").toString();
}
void Store::SetLastSelected(const QString & Rom)
{
    m_Json.insert("lastSelected", Rom);
    Touch();
}
QString Store::Language() const
{
    return m_Json.value("language").toString("auto");
}
void Store::SetLanguage(const QString & Code)
{
    m_Json.insert("language", Code);
    Vortex::SetLanguage(Code);
    Touch();
}
QString Store::Page() const
{
    return m_Json.value("page").toString("library");
}
void Store::SetPage(const QString & Page)
{
    m_Json.insert("page", Page);
    Touch();
}
QString Store::CoverDirectory() const
{
    return QFileInfo(m_Path).absolutePath() + QStringLiteral("/covers");
}
QJsonValue Store::Value(const QString & Key, const QJsonValue & Default) const
{
    return m_Json.contains(Key) ? m_Json.value(Key) : Default;
}
void Store::SetValue(const QString & Key, const QJsonValue & Value)
{
    m_Json.insert(Key, Value);
    Touch();
}
} // namespace Vortex
