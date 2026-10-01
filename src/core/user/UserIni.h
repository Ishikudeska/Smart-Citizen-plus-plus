#pragma once

#include "core/model/StringEntry.h"
#include "core/text/IniFile.h"

#include <QDateTime>
#include <QFileInfoList>
#include <QList>
#include <QString>

#include <optional>

namespace core {

// The channel's user.ini: every hand-made edit, as key=value lines in UTF-8
// (no BOM) with CRLF line ends and values kept verbatim. Before any write
// that changes it, the current file is snapshotted into the channel's
// backups folder as "user.ini.bak_YYYYMMDD_HHMMSS_ffffff" (newest 5 kept),
// because it is the one file whose loss can't be regenerated (#172). Ports
// user_ini_manager.py.
class UserIni
{
public:
    static constexpr int kKeepBackups = 5;

    explicit UserIni(QString path);

    const QString &path() const { return path_; }
    QString backupsDir() const; // <channel>/backups, next to user.ini

    IniMap load() const;

    // Writes the entries whose custom value differs from the original, in
    // list order. Returns the number written, or nothing on failure.
    std::optional<int> save(const QList<StringEntry> &entries) const;
    std::optional<int> save(const IniMap &values) const;

    // The close-time autosave must not turn a populated user.ini into an
    // empty one: with no edits in memory but content on disk, something
    // went wrong at load (v1.3.0 data loss), so the write is refused.
    bool shouldAutosave(const QList<StringEntry> &entries) const;

    // Snapshot management.
    QString backup(const QDateTime &now = QDateTime::currentDateTime()) const;
    QFileInfoList backups() const; // newest first
    bool restore(const QString &backupFile) const; // snapshots the current file first

    // Renames user.ini to "user.ini.bak-YYYYMMDD-HHMMSS" (or deletes it when
    // `keepBackup` is false). Returns the backup path; empty when nothing
    // existed or no backup was kept.
    QString reset(bool keepBackup = true, const QDateTime &now = QDateTime::currentDateTime()) const;

    // First-run bootstrap: the keys where `currentGameFile` differs from
    // `referenceBaseIni` become user.ini, if user.ini doesn't exist yet.
    int generateFromDiff(const QString &referenceBaseIni, const QString &currentGameFile) const;

private:
    bool writeText(const QString &text) const;
    void backupIfChanging(const QString &newText) const;

    QString path_;
};

// Copies (or, with `move`, moves) every file from `oldRoot` into `newRoot`
// after the user changes the data folder (#103). Files already present in
// the new location win; a new folder nested inside the old one is handled.
// Returns the number of files transferred.
int migrateUserDataDir(const QString &oldRoot, const QString &newRoot, bool move = false);

} // namespace core
