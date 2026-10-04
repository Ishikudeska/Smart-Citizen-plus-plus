#include "core/user/UserIni.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace core {

namespace {

constexpr QLatin1StringView kBackupPrefix("user.ini.bak_");

QByteArray normalizedNewlines(QByteArray bytes)
{
    bytes.replace("\r\n", "\n");
    bytes.replace('\r', '\n');
    return bytes;
}

} // namespace

UserIni::UserIni(QString path)
    : path_(std::move(path))
{
}

QString UserIni::backupsDir() const
{
    return QFileInfo(path_).dir().filePath(QStringLiteral("backups"));
}

IniMap UserIni::load() const
{
    return loadIni(path_, /*stripValues=*/false);
}

bool UserIni::writeText(const QString &text) const
{
    QDir().mkpath(QFileInfo(path_).absolutePath());
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(toCrlfUtf8(text));
    return file.commit();
}

void UserIni::backupIfChanging(const QString &newText) const
{
    QFile file(path_);
    if (!file.exists() || file.size() == 0 || !file.open(QIODevice::ReadOnly))
        return;
    // Compare bytes with newlines normalized: a corrupt file then simply
    // counts as changing (and gets snapshotted), and an identical save
    // doesn't push real history out of the five slots.
    if (normalizedNewlines(file.readAll()) != newText.toUtf8())
        backup();
}

std::optional<int> UserIni::save(const IniMap &values) const
{
    const QString text = formatIni(values);
    backupIfChanging(text);
    if (!writeText(text))
        return std::nullopt;
    return static_cast<int>(values.size());
}

std::optional<int> UserIni::save(const QList<StringEntry> &entries) const
{
    IniMap edits;
    for (const StringEntry &e : entries)
        if (e.isModified())
            edits.insert(e.key, e.customValue);
    return save(edits);
}

bool UserIni::shouldAutosave(const QList<StringEntry> &entries) const
{
    if (std::any_of(entries.begin(), entries.end(), [](const StringEntry &e) { return e.isModified(); }))
        return true;
    const QFileInfo info(path_);
    return !(info.exists() && info.size() > 0);
}

QString UserIni::backup(const QDateTime &now) const
{
    const QFileInfo info(path_);
    if (!info.exists() || info.size() == 0)
        return {};
    const QDir dir(backupsDir());
    if (!QDir().mkpath(dir.path()))
        return {};

    // Names carry a fixed-width timestamp, so name order is age order.
    QStringList existing = dir.entryList({kBackupPrefix + u'*'}, QDir::Files, QDir::Name);
    while (existing.size() >= kKeepBackups)
        QFile::remove(dir.filePath(existing.takeFirst()));

    const QString stamp = now.toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")) + QStringLiteral("000");
    QString target = dir.filePath(kBackupPrefix + stamp);
    for (int suffix = 2; QFileInfo::exists(target); ++suffix)
        target = dir.filePath(kBackupPrefix + stamp + u'-' + QString::number(suffix));
    if (!QFile::copy(path_, target))
        return {};
    return target;
}

QFileInfoList UserIni::backups() const
{
    QFileInfoList list = QDir(backupsDir()).entryInfoList({kBackupPrefix + u'*'}, QDir::Files, QDir::Name);
    std::reverse(list.begin(), list.end());
    return list;
}

bool UserIni::restore(const QString &backupFile) const
{
    backup();
    QDir().mkpath(QFileInfo(path_).absolutePath());
    if (QFileInfo::exists(path_) && !QFile::remove(path_))
        return false;
    return QFile::copy(backupFile, path_);
}

QString UserIni::reset(bool keepBackup, const QDateTime &now) const
{
    if (!QFileInfo::exists(path_))
        return {};
    if (!keepBackup) {
        QFile::remove(path_);
        return {};
    }
    const QString base = path_ + QStringLiteral(".bak-") + now.toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString target = base;
    for (int suffix = 2; QFileInfo::exists(target); ++suffix)
        target = base + u'-' + QString::number(suffix);
    if (!QFile::rename(path_, target))
        return {};
    return target;
}

int UserIni::generateFromDiff(const QString &referenceBaseIni, const QString &currentGameFile) const
{
    if (!QFileInfo::exists(referenceBaseIni) || !QFileInfo::exists(currentGameFile) || QFileInfo::exists(path_))
        return 0;
    const IniMap reference = loadIni(referenceBaseIni, false);
    const IniMap current = loadIni(currentGameFile, false);
    IniMap diffs;
    for (const auto &[key, value] : current)
        if (value != reference.value(key))
            diffs.insert(key, value);
    if (diffs.isEmpty() || !writeText(formatIni(diffs)))
        return 0;
    return static_cast<int>(diffs.size());
}

int migrateUserDataDir(const QString &oldRoot, const QString &newRoot, bool move)
{
    auto resolved = [](const QString &path) {
        const QFileInfo info(path);
        const QString canonical = info.canonicalFilePath();
        return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
    };
#ifdef Q_OS_WIN
    constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif
    const QString oldResolved = resolved(oldRoot);
    const QString newResolved = resolved(newRoot);
    if (!QFileInfo(oldRoot).isDir() || oldResolved.compare(newResolved, kPathCase) == 0)
        return 0;

    // Snapshot first: when the new folder sits inside the old one, files
    // copied into it must not be picked up again.
    QStringList files;
    for (QDirIterator it(oldResolved, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories); it.hasNext();)
        files << it.next();

    const QString newPrefix = newResolved + u'/';
    const QDir oldDir(oldResolved);
    int transferred = 0;
    for (const QString &src : std::as_const(files)) {
        if (QDir::cleanPath(src).startsWith(newPrefix, kPathCase))
            continue; // already inside the destination
        const QString dest = QDir(newRoot).filePath(oldDir.relativeFilePath(src));
        if (QFileInfo::exists(dest))
            continue; // never clobber data already in the new location
        QDir().mkpath(QFileInfo(dest).absolutePath());
        if (!QFile::copy(src, dest))
            continue;
        if (move)
            QFile::remove(src);
        ++transferred;
    }

    if (move) {
        // Prune emptied folders, deepest first, then the old root itself.
        QStringList dirs;
        for (QDirIterator it(oldResolved, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
             it.hasNext();)
            dirs << it.next();
        std::sort(dirs.begin(), dirs.end(),
                  [](const QString &a, const QString &b) { return a.count(u'/') > b.count(u'/'); });
        for (const QString &d : std::as_const(dirs))
            QDir().rmdir(d);
        QDir().rmdir(oldResolved);
    }
    return transferred;
}

} // namespace core
