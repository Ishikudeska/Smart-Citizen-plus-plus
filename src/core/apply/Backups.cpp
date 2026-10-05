#include "core/apply/Backups.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace core {

namespace {

constexpr QLatin1StringView kPrefix("global.ini.bak_");

QFileInfoList byAge(const QString &dir)
{
    // Oldest first.
    return QDir(dir).entryInfoList({kPrefix + u'*'}, QDir::Files, QDir::Time | QDir::Reversed);
}

} // namespace

QString GameFileBackups::backup(const QString &gameFile, QString *error, const QDateTime &now) const
{
    if (!QFileInfo::exists(gameFile))
        return {};
    if (!QDir().mkpath(dir_)) {
        if (error)
            *error = QStringLiteral("Cannot create %1").arg(dir_);
        return {};
    }

    QFileInfoList existing = byAge(dir_);
    while (existing.size() >= kKeep) {
        QFile::remove(existing.takeFirst().absoluteFilePath());
    }

    const QString target = QDir(dir_).filePath(kPrefix + now.toString(QStringLiteral("yyyyMMdd_HHmmss")));
    QFile::remove(target); // a second apply in the same second replaces the first copy
    if (!QFile::copy(gameFile, target)) {
        if (error)
            *error = QStringLiteral("Cannot back up %1 to %2").arg(gameFile, target);
        return {};
    }
    return target;
}

QFileInfoList GameFileBackups::list() const
{
    return QDir(dir_).entryInfoList({kPrefix + u'*'}, QDir::Files, QDir::Time);
}

QString GameFileBackups::restore(const QString &backupFile, const QString &gameFile)
{
    QDir().mkpath(QFileInfo(gameFile).absolutePath());
    if (QFileInfo::exists(gameFile) && !QFile::remove(gameFile))
        return QStringLiteral("Cannot replace %1").arg(gameFile);
    if (!QFile::copy(backupFile, gameFile))
        return QStringLiteral("Cannot copy %1 to %2").arg(backupFile, gameFile);
    return {};
}

} // namespace core
