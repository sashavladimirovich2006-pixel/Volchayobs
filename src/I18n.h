#pragma once

#include <QHash>
#include <QString>
#include <QTranslator>

class QApplication;

namespace lumen {

// UI languages we ship. English is the source language: all `tr()`
// strings in the codebase are written in English and act as keys for
// the Ukrainian translation table when the user picks Ukrainian.
enum class Language {
    English,    // default — pass-through, no translation
    Ukrainian,  // Українська
};

QString languageDisplayName(Language l);
QString languageCode(Language l);                 // "en" / "uk"
Language languageFromCode(const QString& code);   // unknown -> English

// Custom translator: instead of loading a .qm file, we hold a static
// English -> Ukrainian dictionary. This avoids needing lupdate /
// lrelease at build time and keeps the CI workflow simple. Adding a
// new translated string is just a one-line entry in the dict.
class HashTranslator : public QTranslator {
public:
    explicit HashTranslator(QObject* parent = nullptr);

    void setLanguage(Language l);
    Language language() const { return m_lang; }

    // QTranslator API — looked up by Qt for every QObject::tr() call.
    QString translate(const char* context,
                      const char* sourceText,
                      const char* disambiguation = nullptr,
                      int n = -1) const override;

    bool isEmpty() const override { return m_lang == Language::English; }

private:
    void rebuildDict();

    Language m_lang = Language::English;
    // Key is the *source* English string (UTF-8) so we don't have to
    // care about C++ string vs QString hashing rules.
    QHash<QByteArray, QString> m_dict;
};

// Install the singleton translator on the QApplication and load the
// saved language from QSettings. Called once at startup.
HashTranslator& installTranslator(QApplication* app);

// Apply a new language right away. Most widgets won't retranslate
// themselves at runtime, so callers should warn the user that a
// restart is needed for the change to take effect everywhere.
void applyLanguage(Language l);

} // namespace lumen
