#include "core/ScInstall.h"

#include "core/Settings.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>

#include <algorithm>

namespace core {

namespace {

constexpr QLatin1StringView kCommonSubpaths[] = {
    QLatin1StringView("Program Files/Roberts Space Industries/StarCitizen"),
    QLatin1StringView("Program Files (x86)/Roberts Space Industries/StarCitizen"),
    QLatin1StringView("Roberts Space Industries/StarCitizen"),
    QLatin1StringView("Games/Roberts Space Industries/StarCitizen"),
};

qint64 newestP4kMtime(const QString &root)
{
    qint64 newest = 0;
    for (const auto channelList = channels(); const QString &channel : channelList) {
        const QFileInfo p4k(QDir(root).filePath(channel + QStringLiteral("/Data.p4k")));
        if (p4k.exists())
            newest = std::max(newest, p4k.lastModified().toMSecsSinceEpoch());
    }
    return newest;
}

} // namespace

bool isScInstallRoot(const QString &path)
{
    if (path.isEmpty() || !QFileInfo(path).isDir())
        return false;
    const QDir dir(path);
    const QStringList all = channels();
    return std::any_of(all.cbegin(), all.cend(),
                       [&dir](const QString &channel) { return QFileInfo(dir.filePath(channel)).isDir(); });
}

bool endsInChannel(const QString &path)
{
    const QString name = QFileInfo(QDir::cleanPath(path)).fileName();
    const QStringList all = channels();
    return std::any_of(all.cbegin(), all.cend(), [&name](const QString &channel) {
        return name.compare(channel, Qt::CaseInsensitive) == 0;
    });
}

QString normalizeInstallRoot(const QString &path)
{
    if (path.isEmpty())
        return {};
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (endsInChannel(clean)) {
        const QString parent = QFileInfo(clean).path();
        if (isScInstallRoot(parent))
            return parent;
    }
    return isScInstallRoot(clean) ? clean : QString();
}

QStringList installedChannels(const QString &root)
{
    QStringList out;
    if (root.isEmpty())
        return out;
    for (const auto channelList = channels(); const QString &channel : channelList)
        if (QFileInfo::exists(QDir(root).filePath(channel + QStringLiteral("/Data.p4k"))))
            out << channel;
    return out;
}

QStringList scanCommonInstallLocations()
{
    QStringList found;
    for (const auto volumes = QStorageInfo::mountedVolumes(); const QStorageInfo &volume : volumes) {
        if (!volume.isValid() || !volume.isReady())
            continue;
        for (const QLatin1StringView sub : kCommonSubpaths) {
            const QString candidate = QDir(volume.rootPath()).filePath(sub);
            if (isScInstallRoot(candidate))
                found << QDir::cleanPath(candidate);
        }
    }
    return found;
}

QString pickLiveInstall(const QStringList &candidates)
{
    QString best;
    qint64 bestTime = -1;
    for (const QString &c : candidates) {
        const qint64 t = newestP4kMtime(c);
        if (t > bestTime) {
            best = c;
            bestTime = t;
        }
    }
    return best;
}

QString locateScInstall()
{
    return pickLiveInstall(scanCommonInstallLocations());
}

} // namespace core
