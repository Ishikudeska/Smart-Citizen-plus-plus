#pragma once

#include <QString>
#include <QStringView>

// Python string semantics the Smart Citizen output depends on. Where Python
// and Qt disagree (what counts as whitespace, how strip() works), the
// Python behaviour wins so ported code produces the same text.
namespace core::py {

// str.isspace() for one character: Unicode whitespace plus the C0
// separators U+001C..U+001F that Python also counts.
bool isSpace(QChar c);

// str.strip() / lstrip() / rstrip() with no argument.
QString strip(QStringView s);
QString lstrip(QStringView s);
QString rstrip(QStringView s);

// str.rstrip(chars) for a single character.
QStringView rstrip(QStringView s, QChar c);

} // namespace core::py
