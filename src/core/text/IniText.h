#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

namespace core {

enum class IniEncoding {
    Utf8,         // clean UTF-8 (a leading BOM is dropped)
    Utf8Repaired, // UTF-8 with a few corrupt bytes, each replaced by U+FFFD
    Windows1252,  // not UTF-8 at all; decoded as ANSI
};

struct IniText
{
    QString text;
    IniEncoding encoding = IniEncoding::Utf8;
    int replaced = 0; // U+FFFD characters introduced by a repair
};

// Decodes INI bytes the way Smart Citizen's read_ini_text does (#251).
// Clean UTF-8 is the norm. A file that isn't must be one of two things, and
// they need opposite fallbacks, so the choice is made by measuring:
//  - a few corrupt bytes in otherwise valid UTF-8: decode as UTF-8 with
//    replacement, so only the bad bytes degrade (a cp1252 decode would turn
//    every legitimate "é" into "Ã©");
//  - a genuinely ANSI (Windows-1252) file: nearly every high byte is invalid
//    UTF-8, so decode it as cp1252 instead.
// Rule: if the replacement count is at most half the number of high bytes,
// it is repaired UTF-8.
IniText decodeIniText(const QByteArray &bytes);

// Reads and decodes a file; nothing if it can't be read.
std::optional<IniText> readIniText(const QString &path);

// Windows-1252 to Unicode (undefined bytes become U+FFFD, as Python's
// errors="replace" does).
QString decodeWindows1252(QByteArrayView bytes);

} // namespace core
