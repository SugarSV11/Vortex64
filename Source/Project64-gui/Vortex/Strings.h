#pragma once

#include <QString>

// The texts of Vortex64 come in Spanish (the reference of the design) and English; the language is the
// "language" key of the store ("auto" follows the system). Every text is written next to its translation
// at the place where it is used: Tr("Biblioteca", "Library").
namespace Vortex
{
enum class Lang
{
    Spanish,
    English
};
Lang CurrentLanguage();
void SetLanguage(const QString & Code); // "auto", "es", "en"

bool TextPairsRecorded();                                      // (Language.h: RecordTextPairs, for the tests)
void NoteTextPair(const char * Spanish, const char * English);

inline QString Tr(const char * Spanish, const char * English)
{
    if (TextPairsRecorded())
    {
        NoteTextPair(Spanish, English);
    }
    return QString::fromUtf8(CurrentLanguage() == Lang::Spanish ? Spanish : English);
}

// A text written in English, translated through the catalog (Catalog.inc): Tr("Address"). For the places where the
// text is long or there are many (the debugger), so the code reads in English and the Spanish is kept in one table.
// An English text with no entry is shown as it is and recorded (MissingTranslations(), Language.h) so the tests find it.
QString Tr(const char * English);
} // namespace Vortex
