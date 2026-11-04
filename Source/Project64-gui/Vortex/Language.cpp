#include "Language.h"
#include "Store.h"
#include "Strings.h"

#include <Project64-core/Multilanguage.h>

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QTranslator>
#include <algorithm>
#include <atomic>

namespace Vortex
{
namespace
{
// English text -> Spanish text of what is written once, in English, where it is used: Tr("Address")
struct CatalogEntry
{
    const char * English;
    const char * Spanish;
};
const CatalogEntry g_Catalog[] = {
#include "Catalog.inc"
};

QMutex g_MissingLock;
QSet<QString> g_Missing;

const QHash<QByteArray, const char *> & CatalogIndex()
{
    static const QHash<QByteArray, const char *> Index = []() {
        QHash<QByteArray, const char *> Result;
        for (const CatalogEntry & Entry : g_Catalog)
        {
            Result.insert(Entry.English, Entry.Spanish);
        }
        return Result;
    }();
    return Index;
}

// The translator of the standard texts of Qt (OK, Cancel, Yes, No, the file dialog, ...)
class QtTexts
{
public:
    static void Select(const QString & Code)
    {
        static QTranslator * Translator = nullptr;
        if (Translator != nullptr)
        {
            QCoreApplication::removeTranslator(Translator);
            delete Translator;
            Translator = nullptr;
        }
        if (Code == QLatin1String("es") && QCoreApplication::instance() != nullptr)
        {
            Translator = new QTranslator(QCoreApplication::instance());
            if (Translator->load(QStringLiteral("qtbase_es"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            {
                QCoreApplication::installTranslator(Translator);
            }
            else
            {
                delete Translator;
                Translator = nullptr;
            }
        }
    }
};
} // namespace

QMutex g_PairsLock;
std::atomic<bool> g_PairsOn{false};
QSet<QPair<QString, QString>> g_Pairs;

QString Tr(const char * English)
{
    if (CurrentLanguage() != Lang::Spanish)
    {
        return QString::fromUtf8(English);
    }
    const auto & Index = CatalogIndex();
    const auto It = Index.constFind(QByteArray(English));
    if (It == Index.constEnd())
    {
        QMutexLocker Lock(&g_MissingLock);
        g_Missing.insert(QString::fromUtf8(English));
        return QString::fromUtf8(English);
    }
    return QString::fromUtf8(It.value());
}

void RecordTextPairs(bool On)
{
    g_PairsOn = On;
}

bool TextPairsRecorded()
{
    return g_PairsOn;
}

void NoteTextPair(const char * Spanish, const char * English)
{
    QMutexLocker Lock(&g_PairsLock);
    g_Pairs.insert({QString::fromUtf8(Spanish), QString::fromUtf8(English)});
}

QList<QPair<QString, QString>> TextPairs()
{
    QMutexLocker Lock(&g_PairsLock);
    QList<QPair<QString, QString>> Result(g_Pairs.begin(), g_Pairs.end());
    for (const CatalogEntry & Entry : g_Catalog)
    {
        Result.append({QString::fromUtf8(Entry.Spanish), QString::fromUtf8(Entry.English)});
    }
    return Result;
}

QStringList MissingTranslations()
{
    QMutexLocker Lock(&g_MissingLock);
    QStringList Result = QStringList(g_Missing.begin(), g_Missing.end());
    Result.sort();
    return Result;
}

void ClearMissingTranslations()
{
    QMutexLocker Lock(&g_MissingLock);
    g_Missing.clear();
}

QStringList SupportedLanguages()
{
    return {QStringLiteral("es"), QStringLiteral("en")};
}

QString LanguageName(const QString & Code)
{
    return Code == QLatin1String("es") ? QString::fromUtf8("Español") : QStringLiteral("English");
}

QString ResolveLanguage(const QString & Code)
{
    if (Code == QLatin1String("es") || Code == QLatin1String("en"))
    {
        return Code;
    }
    return QLocale::system().language() == QLocale::Spanish ? QStringLiteral("es") : QStringLiteral("en");
}

QString LanguageCode()
{
    return CurrentLanguage() == Lang::Spanish ? QStringLiteral("es") : QStringLiteral("en");
}

LanguageNotifier & LanguageNotifier::Instance()
{
    static LanguageNotifier * Notifier = new LanguageNotifier;
    return *Notifier;
}

void SyncCoreLanguage(bool OnlyIfChosen)
{
    if (g_Lang == nullptr || (OnlyIfChosen && !g_Lang->IsLanguageLoaded()))
    {
        return;
    }
    const std::string Name = LanguageName(LanguageCode()).toUtf8().constData();
    for (LanguageFile & File : g_Lang->GetLangList())
    {
        if (File.LanguageName == Name)
        {
            if (!g_Lang->IsCurrentLang(File) || !g_Lang->IsLanguageLoaded())
            {
                g_Lang->SetLanguage(Name.c_str());
            }
            return;
        }
    }
}

void ApplyLanguage(const QString & Code)
{
    const QString Before = LanguageCode();
    const bool First = QCoreApplication::instance() != nullptr && QCoreApplication::instance()->property("vortexQtTexts").isNull();
    Store::Instance().SetLanguage(Code); // keeps it (the key of the store) and switches the texts (SetLanguage of Strings.h)
    const QString After = LanguageCode();
    if (First && QCoreApplication::instance() != nullptr)
    {
        QCoreApplication::instance()->setProperty("vortexQtTexts", true);
    }
    if (After != Before || First)
    {
        QtTexts::Select(After);
    }
    SyncCoreLanguage(false); // (also when the language is the one Vortex64 already had: the core may still have another one)
    if (After != Before)
    {
        Q_EMIT LanguageNotifier::Instance().Changed();
    }
}
} // namespace Vortex
