#include "core/text/PyText.h"

#include <QList>

#include <algorithm>

namespace core::py {

bool isSpace(QChar c)
{
    const char16_t u = c.unicode();
    if (u >= 0x1C && u <= 0x1F)
        return true;
    return c.isSpace();
}

QString strip(QStringView s)
{
    qsizetype begin = 0;
    qsizetype end = s.size();
    while (begin < end && isSpace(s[begin]))
        ++begin;
    while (end > begin && isSpace(s[end - 1]))
        --end;
    return s.sliced(begin, end - begin).toString();
}

QString lstrip(QStringView s)
{
    qsizetype begin = 0;
    while (begin < s.size() && isSpace(s[begin]))
        ++begin;
    return s.sliced(begin).toString();
}

QString rstrip(QStringView s)
{
    qsizetype end = s.size();
    while (end > 0 && isSpace(s[end - 1]))
        --end;
    return s.first(end).toString();
}

QStringView rstrip(QStringView s, QChar c)
{
    qsizetype end = s.size();
    while (end > 0 && s[end - 1] == c)
        --end;
    return s.first(end);
}

qsizetype len(QStringView s)
{
    qsizetype n = s.size();
    for (const QChar c : s)
        n -= c.isLowSurrogate() ? 1 : 0;
    return n;
}

bool less(QStringView a, QStringView b)
{
    const qsizetype n = std::min(a.size(), b.size());
    for (qsizetype i = 0; i < n; ++i) {
        if (a[i] == b[i])
            continue;
        // Surrogates encode code points above every other BMP character.
        const auto rank = [](QChar c) { return c.isSurrogate() ? 0x10000 + c.unicode() : c.unicode() + 0; };
        return rank(a[i]) < rank(b[i]);
    }
    return a.size() < b.size();
}

namespace {

bool isCased(char32_t c)
{
    return QChar::isUpper(c) || QChar::isLower(c) || QChar::isTitleCase(c);
}

QString one(char32_t c)
{
    return QString::fromUcs4(&c, 1);
}

QString titleOf(char32_t c)
{
    return one(QChar::toTitleCase(c));
}

} // namespace

QString rePattern(const QString &pattern)
{
    QString out;
    out.reserve(pattern.size() + 16);
    bool inClass = false;
    for (qsizetype i = 0; i < pattern.size(); ++i) {
        const QChar c = pattern[i];
        if (c == u'\\' && i + 1 < pattern.size()) {
            const QChar next = pattern[i + 1];
            ++i;
            if (next == u's')
                out += inClass ? QStringLiteral(R"(\s\x{1c}-\x{1f}\x{85})") : kReSpace;
            else
                out += c, out += next;
            continue;
        }
        if (c == u'[' && !inClass) {
            inClass = true;
            out += c;
            // A leading ']' (or "^]") is a literal member.
            if (i + 1 < pattern.size() && pattern[i + 1] == u'^')
                out += pattern[++i];
            if (i + 1 < pattern.size() && pattern[i + 1] == u']')
                out += pattern[++i];
            continue;
        }
        if (c == u']' && inClass)
            inClass = false;
        out += c;
    }
    return out;
}

QRegularExpression re(const QString &pattern, QRegularExpression::PatternOptions options)
{
    return QRegularExpression(rePattern(pattern), options | QRegularExpression::UseUnicodePropertiesOption);
}

QString capitalize(QStringView s)
{
    const QList<uint> ucs = s.toUcs4();
    QString out;
    for (qsizetype i = 0; i < ucs.size(); ++i)
        out += i == 0 ? titleOf(ucs[i]) : one(ucs[i]).toLower();
    return out;
}

QString title(QStringView s)
{
    QString out;
    bool previousCased = false;
    for (const auto ucs = s.toUcs4(); const uint c : ucs) {
        if (previousCased)
            out += one(c).toLower();
        else
            out += isCased(c) ? titleOf(c) : one(c);
        previousCased = isCased(c);
    }
    return out;
}

} // namespace core::py
