#pragma once

#include <QString>
#include <QStringView>

namespace core::py {

// A JSON string literal exactly as Python's json.dumps writes it: \" \\ \n
// \r \t \b \f, other control characters as \u00XX, and with `ensureAscii`
// every character outside ' '..'~' as \uXXXX (surrogate pairs for astral
// characters). Used where Smart Citizen hashes or stores json.dumps output.
QString jsonString(QStringView s, bool ensureAscii);

inline QString jsonBool(bool b)
{
    return b ? QStringLiteral("true") : QStringLiteral("false");
}

} // namespace core::py
