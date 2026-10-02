#include "core/net/AppUpdater.h"

#include "core/AppIdentity.h"
#include "core/net/Downloader.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

namespace core::net {

std::optional<Version> parseVersion(const QString &text)
{
    QString s = text.trimmed();
    while (s.startsWith(u'v') || s.startsWith(u'V'))
        s.remove(0, 1);
    const QStringList parts = s.split(u'.');
    if (parts.size() < 2)
        return std::nullopt;
    Version v{0, 0, 0};
    for (qsizetype i = 0; i < std::min<qsizetype>(parts.size(), 3); ++i) {
        bool ok = false;
        // int() in Python accepts surrounding whitespace and a sign; digits are what matter here.
        v[static_cast<std::size_t>(i)] = parts[i].trimmed().toInt(&ok);
        if (!ok)
            return std::nullopt;
    }
    return v;
}

bool isNewer(const QString &latest, const QString &current)
{
    const auto l = parseVersion(latest), c = parseVersion(current);
    return l && c && *l > *c;
}

std::optional<InstallerAsset> pickInstallerAsset(const QJsonArray &assets, const QString &exeName)
{
    const QRegularExpression pattern(
        QRegularExpression::anchoredPattern(QRegularExpression::escape(exeName.toLower()) + QStringLiteral("-.+-setup\\.exe")),
        QRegularExpression::CaseInsensitiveOption);
    for (const QJsonValue &v : assets) {
        const QJsonObject a = v.toObject();
        const QString name = a.value(u"name").toString().trimmed();
        const QString url = a.value(u"browser_download_url").toString().trimmed();
        if (!url.isEmpty() && pattern.match(name).hasMatch())
            return InstallerAsset{QUrl(url), static_cast<qint64>(a.value(u"size").toDouble())};
    }
    return std::nullopt;
}

QUrl latestReleaseApi(const QString &repo)
{
    const QString r = repo.trimmed();
    return r.isEmpty() ? QUrl() : QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(r));
}

namespace {

// Runs `reply` to completion, aborting on cancel.
void wait(QNetworkReply *reply, const std::atomic<bool> *cancel)
{
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer poll;
    if (cancel) {
        QObject::connect(&poll, &QTimer::timeout, reply, [reply, cancel] {
            if (cancel->load())
                reply->abort();
        });
        poll.start(100);
    }
    if (!reply->isFinished())
        loop.exec();
}

} // namespace

UpdateCheck checkForUpdate(const QString &repo, const QString &currentVersion, int timeoutMs)
{
    UpdateCheck out;
    const QUrl api = latestReleaseApi(repo);
    if (api.isEmpty()) {
        out.status = UpdateCheck::Status::Disabled;
        return out;
    }
    QNetworkRequest request(api);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(timeoutMs);
    QNetworkAccessManager nam;
    std::unique_ptr<QNetworkReply> reply(nam.get(request));
    wait(reply.get(), nullptr);
    if (reply->error() != QNetworkReply::NoError) {
        out.error = reply->errorString();
        return out;
    }
    QJsonParseError parseError;
    const QJsonObject payload = QJsonDocument::fromJson(reply->readAll(), &parseError).object();
    if (parseError.error != QJsonParseError::NoError) {
        out.error = parseError.errorString();
        return out;
    }
    const QString tag = payload.value(u"tag_name").toString().trimmed();
    if (tag.isEmpty()) {
        out.error = QStringLiteral("Release payload missing tag_name");
        return out;
    }
    out.latest = tag;
    while (out.latest.startsWith(u'v') || out.latest.startsWith(u'V'))
        out.latest.remove(0, 1);
    out.releasePage = QUrl(payload.value(u"html_url").toString().trimmed());
    out.notes = payload.value(u"body").toString().trimmed();
    if (isNewer(tag, currentVersion)) {
        out.status = UpdateCheck::Status::Available;
        out.installer = pickInstallerAsset(payload.value(u"assets").toArray(), QString::fromLatin1(identity::kExeName));
    } else {
        out.status = UpdateCheck::Status::UpToDate;
    }
    return out;
}

QString downloadInstaller(const InstallerAsset &asset, const QString &destDir, QString *error,
                          const DownloadProgress &progress, const std::atomic<bool> *cancel)
{
    QDir().mkpath(destDir);
    QString name = QFileInfo(asset.url.path()).fileName();
    if (!name.endsWith(u".exe", Qt::CaseInsensitive))
        name = QStringLiteral("Setup.exe");
    const QString target = QDir(destDir).filePath(name);
    const QString part = target + QStringLiteral(".part");

    QFile file(part);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = file.errorString();
        return {};
    }
    QNetworkRequest request(asset.url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setRawHeader("Accept", "application/octet-stream");
    request.setTransferTimeout(30'000);
    QNetworkAccessManager nam;
    std::unique_ptr<QNetworkReply> reply(nam.get(request));
    bool writeFailed = false;
    QObject::connect(reply.get(), &QNetworkReply::readyRead, reply.get(), [&] {
        const QByteArray chunk = reply->readAll();
        if (file.write(chunk) != chunk.size() && !writeFailed) {
            writeFailed = true;
            reply->abort();
        }
    });
    QObject::connect(reply.get(), &QNetworkReply::downloadProgress, reply.get(), [&](qint64 done, qint64 total) {
        if (progress)
            progress(done, total > 0 ? total : asset.size);
    });
    wait(reply.get(), cancel);
    file.write(reply->readAll());
    file.close();

    const auto fail = [&](const QString &message) {
        QFile::remove(part);
        *error = message;
        return QString();
    };
    if (cancel && cancel->load())
        return fail(QStringLiteral("Download cancelled"));
    if (writeFailed)
        return fail(file.errorString());
    if (reply->error() != QNetworkReply::NoError)
        return fail(reply->errorString());
    QFile::remove(target);
    if (!QFile::rename(part, target))
        return fail(QStringLiteral("Could not move the installer into place: %1").arg(target));
    return target;
}

} // namespace core::net
