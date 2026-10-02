#include "core/util/OneDrive.h"

#include <QDir>

namespace core::onedrive {

namespace {

// os.path.normcase(os.path.normpath(p)) on Windows.
QString normalized(const QString &path)
{
    return QDir::toNativeSeparators(QDir::cleanPath(QDir::fromNativeSeparators(path))).toLower();
}

bool isOneDriveSegment(const QString &segment)
{
    const QString low = segment.trimmed().toLower();
    return low == u"onedrive" || low.startsWith(u"onedrive - ") || low.startsWith(u"onedrive-");
}

} // namespace

QStringList roots(const QProcessEnvironment &env)
{
    QStringList out, seen;
    for (const char *var : {"OneDrive", "OneDriveConsumer", "OneDriveCommercial"}) {
        const QString value = env.value(QString::fromLatin1(var));
        if (value.isEmpty())
            continue;
        const QString key = normalized(value);
        if (seen.contains(key))
            continue;
        seen << key;
        out << value;
    }
    return out;
}

bool isOneDrivePath(const QString &path, const QProcessEnvironment &env)
{
    if (path.isEmpty())
        return false;
    const QString norm = normalized(path);
    for (const QString &root : roots(env)) {
        const QString r = normalized(root);
        if (norm == r || norm.startsWith(r + u'\\'))
            return true;
    }
    for (const QString &segment : norm.split(u'\\', Qt::SkipEmptyParts))
        if (isOneDriveSegment(segment))
            return true;
    return false;
}

QString suggestLocalDataDir(const QString &appName, const QProcessEnvironment &env)
{
    QString profile = env.value(QStringLiteral("USERPROFILE"));
    if (profile.isEmpty())
        profile = QDir::homePath();
    return QDir::toNativeSeparators(QDir(profile).filePath(QStringLiteral("Documents/") + appName));
}

} // namespace core::onedrive
