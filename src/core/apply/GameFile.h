#pragma once

#include "core/text/IniFile.h"

#include <QSet>
#include <QString>

namespace core {

// Builds the global.ini the game reads: base.ini's lines in base.ini's order,
// each key's value replaced from `merged` when present, ",metadata" key
// suffixes dropped, comments and blank lines kept. UTF-8 with a BOM (the
// game's loader needs it, #261) and CRLF. Keys absent from base.ini are
// never written. Ports merge_ini_files.
QByteArray renderGameFile(const QString &baseIniText, const IniMap &merged);

// renderGameFile over `baseIniPath`, written atomically to `outputPath`.
// Returns an error message, empty on success.
QString writeGameFile(const QString &baseIniPath, const IniMap &merged, const QString &outputPath);

// Checks a written global.ini: it must start with the UTF-8 BOM and hold
// exactly base.ini's keys (values may differ). Returns a readable problem
// report, empty when valid. Ports validate_applied_file.
QString validateGameFile(const QString &writtenPath, const QSet<QString> &stockKeys);

} // namespace core
