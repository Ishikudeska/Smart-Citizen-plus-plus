#include "core/apply/UserCfg.h"

#include "core/text/IniFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

namespace core {

namespace {

const QRegularExpression &keyRe()
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*g_language\s*=)"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

const QRegularExpression &valueRe()
{
    // The pattern contains `)"`, so the raw string needs its own delimiter.
    static const QRegularExpression re(QStringLiteral(R"re(^\s*g_language\s*=\s*"?([^";\r\n]+?)"?\s*(?:[;#].*)?$)re"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

// str.splitlines() for the line breaks a .cfg realistically contains.
QStringList splitLines(const QString &text)
{
    QStringList lines;
    qsizetype start = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        if (c == u'\n' || c == u'\r') {
            lines << text.mid(start, i - start);
            if (c == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n')
                ++i;
            start = i + 1;
        }
    }
    if (start < text.size())
        lines << text.mid(start);
    return lines;
}

bool writeLines(const QString &path, const QStringList &lines)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(toCrlfUtf8(lines.join(u'\n') + u'\n'));
    return file.commit();
}

} // namespace

UserCfgResult ensureUserCfgLanguage(const QString &channelDir, const QString &scLanguageId)
{
    if (channelDir.isEmpty() || !QFileInfo(channelDir).isDir())
        return UserCfgResult::Failed;

    const QString path = QDir(channelDir).filePath(QStringLiteral("user.cfg"));
    const QString languageLine = QStringLiteral("g_language = %1").arg(scLanguageId);

    QFile file(path);
    if (!file.exists())
        return writeLines(path, {languageLine}) ? UserCfgResult::Created : UserCfgResult::Failed;
    if (!file.open(QIODevice::ReadOnly))
        return UserCfgResult::Failed;
    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    QStringList lines = splitLines(content);
    for (const QString &line : std::as_const(lines)) {
        if (!keyRe().match(line).hasMatch())
            continue;
        const auto m = valueRe().match(line);
        const QString existing = m.hasMatch() ? m.captured(1).trimmed() : QString();
        if (existing.compare(scLanguageId, Qt::CaseInsensitive) == 0)
            return UserCfgResult::Unchanged;
        for (QString &l : lines)
            if (keyRe().match(l).hasMatch())
                l = languageLine;
        return writeLines(path, lines) ? UserCfgResult::Updated : UserCfgResult::Failed;
    }

    if (!lines.isEmpty() && !lines.last().trimmed().isEmpty())
        lines << QString();
    lines << languageLine;
    return writeLines(path, lines) ? UserCfgResult::Added : UserCfgResult::Failed;
}

} // namespace core
