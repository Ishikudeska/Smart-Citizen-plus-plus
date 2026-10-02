#include "core/text/PyJson.h"

namespace core::py {

QString jsonString(QStringView s, bool ensureAscii)
{
    QString out;
    out.reserve(s.size() + 2);
    out += u'"';
    for (const QChar c : s) {
        const char16_t u = c.unicode();
        switch (u) {
        case u'"': out += QStringLiteral("\\\""); continue;
        case u'\\': out += QStringLiteral("\\\\"); continue;
        case u'\n': out += QStringLiteral("\\n"); continue;
        case u'\r': out += QStringLiteral("\\r"); continue;
        case u'\t': out += QStringLiteral("\\t"); continue;
        case u'\b': out += QStringLiteral("\\b"); continue;
        case u'\f': out += QStringLiteral("\\f"); continue;
        default: break;
        }
        if (u < 0x20 || (ensureAscii && u > 0x7E))
            out += QStringLiteral("\\u%1").arg(static_cast<int>(u), 4, 16, QChar(u'0'));
        else
            out += c;
    }
    out += u'"';
    return out;
}

} // namespace core::py
