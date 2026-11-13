#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QPair>
#include <QStringList>

// The language of the interface of Vortex64. There are exactly two with a complete translation, Spanish and English;
// nothing else is offered anywhere. The choice is kept in the store ("language": "es", "en" or "auto" = the system's:
// Spanish for a Spanish system, English for any other) and one call changes it for the whole application:
//   ApplyLanguage("es") stores it, switches the texts (Strings.h) and the Qt standard texts (buttons of the standard
//   dialogs) and tells everybody through LanguageNotifier::Changed(); the windows that were built with the old
//   texts are rebuilt by their owners (MainWindow rebuilds its shell, menus and status bar, the dialogs reopen).
namespace Vortex
{
// "es", "en"
QStringList SupportedLanguages();
// The name of a language in that language itself ("Español", "English")
QString LanguageName(const QString & Code);
// The code in force right now ("es" or "en"; never "auto")
QString LanguageCode();
// "auto" resolved against the system
QString ResolveLanguage(const QString & Code);

class LanguageNotifier : public QObject
{
    Q_OBJECT
public:
    static LanguageNotifier & Instance();

Q_SIGNALS:
    void Changed();
};

// The language file of the core (the texts of the settings pages, the shortcuts and the messages of the core) is the one
// of Vortex64's language. ApplyLanguage does it every time; at the start it is done only when a language was chosen before
// (OnlyIfChosen), the first run is ApplyFirstRunDefaults' (Spanish).
void SyncCoreLanguage(bool OnlyIfChosen);

// Chooses the language of the application ("es", "en" or "auto"), keeps it and notifies. Nothing happens when it is already so.
void ApplyLanguage(const QString & Code);

// For the tests that look for texts in the wrong language: while it is on, every Tr(spanish, english) records its two texts
void RecordTextPairs(bool On);
// (Spanish, English) of the Tr(spanish, english) calls made so far and of the catalog (Catalog.inc)
QList<QPair<QString, QString>> TextPairs();

// Texts of the catalog (Strings.h Tr("English")) that were asked for in Spanish and have no translation; for the tests
QStringList MissingTranslations();
void ClearMissingTranslations();
} // namespace Vortex
