#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

// The state of the Vortex64 frontend that the Project64 core does not keep: favourites, the play history
// (last time, accumulated play time), the extra ROM directories of the library, the view mode, the
// sorting, the filter and the last selection. It is one JSON file ($XDG_CONFIG_HOME/vortex64/vortex.json)
// written atomically a moment after each change. The core keeps its own settings (RomList_GameDir, ...).
namespace Vortex
{
struct PlayRecord
{
    QDateTime LastPlayed;
    qint64 Seconds = 0;
    int Launches = 0;
};

class Store : public QObject
{
    Q_OBJECT
public:
    static Store & Instance();
    static QString DefaultPath();
    // Tests point the store somewhere else before first use
    static void SetPath(const QString & Path);

    // ---- favourites
    bool IsFavorite(const QString & Rom) const;
    void SetFavorite(const QString & Rom, bool On);
    QStringList Favorites() const;

    // ---- history
    PlayRecord Record(const QString & Rom) const;
    QStringList RecentRoms() const; // newest first
    void ForgetRecent(const QString & Rom);
    // A game starts / pauses / ends: the play time counts only while the CPU runs and is not paused
    void SessionStart(const QString & Rom);
    void SessionPaused(bool Paused);
    void SessionEnd();
    QString SessionRom() const
    {
        return m_SessionRom;
    }
    qint64 SessionSeconds() const;

    // ---- library directories (besides RomList_GameDir of the core)
    QStringList ExtraDirectories() const;
    void SetExtraDirectories(const QStringList & Directories);

    // ---- interface state
    QString ViewMode() const; // "grid", "list", "compact"
    void SetViewMode(const QString & Mode);
    QString SortKey() const; // "name", "region", "size", "recent", "played"
    bool SortAscending() const;
    void SetSort(const QString & Key, bool Ascending);
    QString Filter() const;
    void SetFilter(const QString & Text);
    QString LastSelected() const;
    void SetLastSelected(const QString & Rom);
    QString Language() const;
    void SetLanguage(const QString & Code);
    QString Page() const; // last page of the sidebar
    void SetPage(const QString & Page);
    QString CoverDirectory() const;

    // Generic access for the pages that keep a little state of their own
    QJsonValue Value(const QString & Key, const QJsonValue & Default = QJsonValue()) const;
    void SetValue(const QString & Key, const QJsonValue & Value);

    void Flush();

Q_SIGNALS:
    void FavoritesChanged();
    void HistoryChanged();
    void DirectoriesChanged();
    void ViewChanged();

private:
    Store();
    void Load();
    void Touch();

    QString m_Path;
    QJsonObject m_Json;
    QSet<QString> m_Favorites;
    QHash<QString, PlayRecord> m_Plays;
    QTimer m_SaveTimer;
    bool m_Dirty = false;
    QString m_SessionRom;
    QDateTime m_SessionStarted;
    qint64 m_SessionAccumulated = 0;
    bool m_SessionPausedNow = false;
};
} // namespace Vortex
