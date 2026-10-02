#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTranslator>
#include <QVariantHash>

#include <atomic>
#include <memory>

// UI text from resources/languages/<lang>/ui.json: a tree of sections whose
// leaves are {"ht": human, "at": AI} objects (plain strings still accepted).
// A leaf shows "ht", else "at"; a blank leaf leaves the English text in
// place, and a missing key shows the key itself. QML asks with
// qsTr("dot.key") and C++ with i18n::tr("dot.key"); both reach the
// JsonTranslator installed on the application. Ports i18n.py.
namespace core::i18n {

inline const QString kDefaultLanguage = QStringLiteral("english");

// One language's strings over the English base, flattened to "a.b.c" keys.
// Immutable once built, so lookups need no locking.
class Catalog
{
public:
    Catalog() = default;

    // English from `languagesDir`, overlaid with `language`. An unreadable
    // or malformed file contributes nothing.
    static Catalog load(const QString &languagesDir, const QString &language);
    // From already-parsed trees (tests).
    static Catalog fromTrees(const QString &language, const QJsonObject &english, const QJsonObject &overlay = {});

    const QString &language() const { return language_; }
    bool isEmpty() const { return strings_.isEmpty(); }
    // The display text, or a null string when the key is missing or blank.
    QString lookup(const QString &key) const { return strings_.value(key); }

private:
    QString language_ = kDefaultLanguage;
    QHash<QString, QString> strings_;
};

// i18n.py's _deep_merge: sections merge, a leaf with text replaces the
// base leaf whole, a blank leaf keeps the base, anything else replaces.
void deepMerge(QJsonObject &base, const QJsonObject &overlay);

// Serves qsTr()/QCoreApplication::translate() from a Catalog. Unknown keys
// (including Qt's own English source strings) are left untranslated.
class JsonTranslator : public QTranslator
{
    Q_OBJECT

public:
    explicit JsonTranslator(QObject *parent = nullptr);

    // Swaps the catalog in atomically. Reinstall the translator (or post a
    // LanguageChange) afterwards so the UI refreshes.
    void setCatalog(std::shared_ptr<const Catalog> catalog);
    std::shared_ptr<const Catalog> catalog() const;

    QString translate(const char *context, const char *sourceText, const char *disambiguation = nullptr,
                      int n = -1) const override;
    bool isEmpty() const override;

private:
    std::atomic<std::shared_ptr<const Catalog>> catalog_;
};

// str.format(**args) for named fields: "{name}", "{count:,}" (thousands
// separators), "{{" and "}}". A field without a value, or a malformed
// template, returns `text` unchanged, like the Python's fallback.
QString format(const QString &text, const QVariantHash &args);

// The text for `key` from the installed translators, formatted with
// `args`; the key itself when no translation exists.
QString tr(const char *key, const QVariantHash &args = {});

// Languages whose ui.json holds at least one string ("_comment" fields do
// not count), plus English; sorted.
QStringList availableLanguages(const QString &languagesDir);

} // namespace core::i18n
