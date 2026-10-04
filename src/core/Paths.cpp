#include "core/Paths.h"

#include "core/AppIdentity.h"
#include "core/Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <utility>

namespace core {

namespace {

QString appName()
{
    return QString::fromLatin1(identity::kAppName);
}

QString expand(const QString &path)
{
    // %VAR% references, as os.path.expandvars accepts on Windows.
    QString out = path;
    qsizetype start = 0;
    while ((start = out.indexOf(u'%', start)) >= 0) {
        const qsizetype end = out.indexOf(u'%', start + 1);
        if (end < 0)
            break;
        const QString name = out.mid(start + 1, end - start - 1);
        const QString value = qEnvironmentVariable(name.toLocal8Bit().constData());
        if (name.isEmpty() || value.isEmpty()) {
            start = end;
            continue;
        }
        out.replace(start, end - start + 1, value);
        start += value.size();
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(out));
}

} // namespace

PathRoots PathRoots::defaults()
{
    PathRoots r;
    r.portable = identity::kPortable;
    r.portableRoot = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data"));
    r.documentsDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    r.localAppData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
#ifdef Q_OS_WIN
    if (const QString env = qEnvironmentVariable("LOCALAPPDATA"); !env.isEmpty())
        r.localAppData = QDir::fromNativeSeparators(env);
#endif
    return r;
}

Paths::Paths(const Settings &settings, PathRoots roots) : settings_(settings), roots_(std::move(roots))
{}

QString Paths::language(const QString &requested) const
{
    return requested.isEmpty() ? settings_.selectedLanguage() : requested;
}

QString Paths::userDataRoot() const
{
    if (roots_.portable)
        return roots_.portableRoot;
    if (const QString override = settings_.userDataDirOverride(); !override.isEmpty())
        return expand(override);
    return QDir(roots_.documentsDir).filePath(appName());
}

QString Paths::channelDataDir() const
{
    return QDir(userDataRoot()).filePath(settings_.activeChannel());
}

QString Paths::cacheDir() const
{
    return QDir(channelDataDir()).filePath(QStringLiteral("cache"));
}

QString Paths::baseIni(const QString &requested) const
{
    const QString lang = language(requested);
    if (lang == kDefaultLanguage)
        return QDir(cacheDir()).filePath(QStringLiteral("base.ini"));
    return QDir(cacheDir()).filePath(QStringLiteral("lang/%1/base.ini").arg(lang));
}

QString Paths::enhancementsDir(const QString &requested) const
{
    // Enhancements sit next to the base.ini they were generated from.
    return QFileInfo(baseIni(requested)).path();
}

QString Paths::userIni() const
{
    return QDir(channelDataDir()).filePath(QStringLiteral("user.ini"));
}

QString Paths::backupsDir() const
{
    return QDir(channelDataDir()).filePath(QStringLiteral("backups"));
}

QString Paths::logsDir() const
{
    return QDir(userDataRoot()).filePath(QStringLiteral("logs"));
}

QString Paths::dataForgeCacheDir() const
{
    QString base;
    if (const QString override = settings_.cacheDirOverride(); !override.isEmpty())
        base = expand(override);
    else if (roots_.portable)
        base = QDir(roots_.portableRoot).filePath(QStringLiteral("cache"));
    else
        base = QDir(roots_.localAppData).filePath(appName());
    return QDir(base).filePath(settings_.activeChannel() + QStringLiteral("/cache/dataforge"));
}

QString Paths::channelInstallDir() const
{
    const QString root = settings_.scInstallRoot();
    if (root.isEmpty())
        return {};
    return QDir(root).filePath(settings_.activeChannel());
}

QString Paths::p4kPath() const
{
    const QString dir = channelInstallDir();
    return dir.isEmpty() ? QString() : QDir(dir).filePath(QStringLiteral("Data.p4k"));
}

QString Paths::gameGlobalIni(const QString &requested) const
{
    const QString dir = channelInstallDir();
    if (dir.isEmpty())
        return {};
    return QDir(dir).filePath(
        QStringLiteral("data/Localization/%1/global.ini").arg(scLanguageId(language(requested))));
}

} // namespace core
