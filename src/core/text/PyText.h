#pragma once

#include <QRegularExpression>
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

// len(): code points, not UTF-16 units.
qsizetype len(QStringView s);

// str < str: code point order (UTF-16 order differs above U+D7FF).
bool less(QStringView a, QStringView b);

// str.capitalize() and str.title().
QString capitalize(QStringView s);
QString title(QStringView s);

// What Python's re module means by \s in a str pattern, for use inside a
// QRegularExpression with UseUnicodePropertiesOption.
inline const QString kReSpace = QStringLiteral(R"([\s\x{1c}-\x{1f}\x{85}])");

// `pattern` as Python's re reads it: \s, \d and \w over Unicode (\s also
// takes the C0 separators Python counts as space), inside or outside a
// character class.
QString rePattern(const QString &pattern);
QRegularExpression re(const QString &pattern, QRegularExpression::PatternOptions options = {});

} // namespace core::py
