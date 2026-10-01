#include "core/apply/GameFile.h"

#include "core/text/IniText.h"
#include "core/text/PyText.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace core {

namespace {

const QByteArray kUtf8Bom("\xEF\xBB\xBF");

QStringList sortedSample(const QSet<QString> &keys, qsizetype limit)
{
    QStringList sorted(keys.cbegin(), keys.cend());
    std::sort(sorted.begin(), sorted.end()); // Python sorts by code point; so does QString's operator<
    return sorted.mid(0, limit);
}

} // namespace

QByteArray renderGameFile(const QString &baseIniText, const IniMap &merged)
{
    QString source = baseIniText;
    source.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    source.replace(u'\r', u'\n');
    QStringList lines = source.split(u'\n');
    // A trailing newline yields one empty last element, which is not a line.
    const bool trailingNewline = !lines.isEmpty() && lines.last().isEmpty();
    if (trailingNewline)
        lines.removeLast();

    QString out;
    out.reserve(source.size() + source.size() / 8);
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString &line = lines[i];
        const bool isLast = i == lines.size() - 1;
        const QStringView ending = (!isLast || trailingNewline) ? QStringView(u"\n") : QStringView();

        const QString stripped = py::strip(line);
        const qsizetype eq = line.indexOf(u'=');
        if (stripped.isEmpty() || stripped.startsWith(u';') || eq < 0) {
            out += line;
            out += ending;
            continue;
        }
        const QString key = py::strip(QStringView(line).first(eq));
        const qsizetype comma = key.indexOf(u',');
        const QString cleanKey = comma < 0 ? key : py::strip(QStringView(key).first(comma));
        out += cleanKey;
        out += u'=';
        if (const QString *value = merged.find(cleanKey))
            out += *value;
        else
            out += QStringView(line).sliced(eq + 1);
        out += ending;
    }
    return kUtf8Bom + toCrlfUtf8(out);
}

QString writeGameFile(const QString &baseIniPath, const IniMap &merged, const QString &outputPath)
{
    const auto base = readIniText(baseIniPath);
    if (!base)
        return QStringLiteral("Cannot read %1").arg(baseIniPath);
    QDir().mkpath(QFileInfo(outputPath).absolutePath());
    QSaveFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("Cannot write %1: %2").arg(outputPath, file.errorString());
    file.write(renderGameFile(base->text, merged));
    if (!file.commit())
        return QStringLiteral("Cannot write %1: %2").arg(outputPath, file.errorString());
    return {};
}

QString validateGameFile(const QString &writtenPath, const QSet<QString> &stockKeys)
{
    QFile file(writtenPath);
    if (!file.open(QIODevice::ReadOnly))
        return {}; // can't check; the caller already wrote it successfully
    const bool hasBom = file.read(3) == kUtf8Bom;
    file.close();

    QStringList lines;
    if (!hasBom)
        lines << QStringLiteral(
            "The written file is missing its UTF-8 BOM. Star Citizen's own localization loader needs this to "
            "detect the file's encoding — without it the game can fail to resolve every string (shown as raw "
            "@KeyName placeholders instead of text) rather than just the ones that changed.");

    const IniMap written = loadIni(writtenPath);
    QSet<QString> writtenKeys;
    writtenKeys.reserve(written.size());
    for (const auto &[key, value] : written)
        writtenKeys.insert(key);
    const QSet<QString> missing = QSet<QString>(stockKeys).subtract(writtenKeys);
    const QSet<QString> extra = QSet<QString>(writtenKeys).subtract(stockKeys);
    if (hasBom && missing.isEmpty() && extra.isEmpty())
        return {};

    auto list = [&lines](const QSet<QString> &keys, const QString &heading) {
        lines << heading.arg(keys.size());
        for (const QString &k : sortedSample(keys, 20))
            lines << QStringLiteral("  ") + k;
        if (keys.size() > 20)
            lines << QStringLiteral("  ... and %1 more").arg(keys.size() - 20);
    };
    if (!missing.isEmpty())
        list(missing, QStringLiteral("%1 key(s) from base.ini are missing from the written file:"));
    if (!extra.isEmpty()) {
        if (!lines.isEmpty())
            lines << QString();
        list(extra, QStringLiteral("%1 unexpected key(s) in written file (not in base.ini):"));
    }
    return lines.join(u'\n');
}

} // namespace core
