#include "core/apply/Stamps.h"

#include "core/model/StringEntry.h"
#include "core/text/PyText.h"

#include <QRegularExpression>

namespace core {

namespace {

constexpr auto kUnicode = QRegularExpression::UseUnicodePropertiesOption;

// Earlier stamps from this app or from Smart Citizen.
QString stampedBy(const QString &appName)
{
    return QStringLiteral("(?:Smart Citizen|%1)").arg(QRegularExpression::escape(appName));
}

} // namespace

void stampJournalEntries(IniMap &merged, const IniMap &stock, const QString &appName, const QString &version)
{
    const QRegularExpression stampRe(
        QStringLiteral(R"((?:\\n)*\[Edited with %1 v[^\]]+\]\s*$)").arg(stampedBy(appName)), kUnicode);
    static const QRegularExpression titleKeyRe(
        QStringLiteral(R"(_(?:title|shorttitle|subtitle|subheading|from)(?:,P)?$)"),
        QRegularExpression::CaseInsensitiveOption | kUnicode);
    const QString newStamp = QStringLiteral(R"(\n\n[Edited with %1 v%2])").arg(appName, version);

    IniMap out;
    out.reserve(merged.size());
    for (const auto &[key, value] : merged) {
        if (extractCategory(key) != category::kJournal || titleKeyRe.match(key).hasMatch()) {
            out.insert(key, value);
            continue;
        }
        QString unstamped = value;
        unstamped.replace(stampRe, QString());
        unstamped = py::rstrip(unstamped);
        const QString *stockValue = stock.find(key);
        if (!stock.isEmpty() && stockValue && *stockValue == unstamped)
            out.insert(key, unstamped); // stock content; nothing of ours to mark
        else
            out.insert(key, unstamped + newStamp);
    }
    merged = std::move(out);
}

void stampFrontendVersion(IniMap &merged, const QString &appName, const QString &version)
{
    const QString *current = merged.find(kFrontendVersionKey);
    if (!current)
        return;
    // Matches today's "\n"-separated form, the older " | " form and the
    // legacy phrasings, with or without a "v" before the version.
    const QRegularExpression stampRe(
        QStringLiteral(
            R"(\s*(?:\|\s*|(?:\\n)+\s*)(?:Localizations Enhanced (?:with|by)|Enhanced with <3 by)\s+%1\s+v?[^\s|]+\s*$)")
            .arg(stampedBy(appName)),
        kUnicode);
    QString base = *current;
    base.replace(stampRe, QString());
    base = py::rstrip(base);
    merged.insert(kFrontendVersionKey,
                  base + QStringLiteral(R"(\nLocalizations Enhanced with %1 v%2)").arg(appName, version));
}

} // namespace core
