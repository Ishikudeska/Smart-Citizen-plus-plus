#include "core/text/PyText.h"

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

} // namespace core::py
