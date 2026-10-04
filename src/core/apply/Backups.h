#pragma once

#include <QDateTime>
#include <QFileInfoList>
#include <QString>

#include <utility>

namespace core {

// Copies of the game's global.ini taken before each apply, in the channel's
// backups folder as "global.ini.bak_YYYYMMDD_HHMMSS". At most `keep` are
// kept; the oldest (by modification time, which the copy preserves from the
// original) is deleted first.
class GameFileBackups
{
public:
    static constexpr int kKeep = 5;

    explicit GameFileBackups(QString backupsDir) : dir_(std::move(backupsDir)) {}

    // Backs up `gameFile` if it exists. Returns the backup's path, empty when
    // there was nothing to back up, or an error via `error`.
    QString backup(const QString &gameFile, QString *error = nullptr,
                   const QDateTime &now = QDateTime::currentDateTime()) const;

    // Newest first.
    QFileInfoList list() const;

    // Copies `backupFile` over `gameFile`. Returns an error message or empty.
    static QString restore(const QString &backupFile, const QString &gameFile);

private:
    QString dir_;
};

} // namespace core
