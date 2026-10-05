#pragma once

#include <QDate>
#include <QString>

#include <expected>

// "Export Loc-Pack": the applied global.ini, zipped on its own at the root,
// for sharing; recipients drop it into their Localization folder whether or
// not they use this app. Ports locpack_exporter.py.
namespace core {

// "<App>-LocPack-<channel>-YYYYMMDD.zip": loc files are channel-specific.
QString defaultLocPackFilename(const QString &channel, QDate today = QDate::currentDate());

// Deflates `sourceGlobalIni` into `outputZip` as "global.ini". Returns the
// source size; an error when the game file is missing (apply first) or the
// write fails.
std::expected<qint64, QString> writeLocPackZip(const QString &sourceGlobalIni, const QString &outputZip);

} // namespace core
