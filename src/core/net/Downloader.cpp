#include "core/net/Downloader.h"

#include "core/AppIdentity.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimeZone>
#include <QTimer>

namespace core::net {

namespace {

QString etagPath(const QString &dest)
{
    return dest + QStringLiteral(".etag");
}

// RFC 7231 IMF-fixdate, always in English.
QByteArray httpDate(const QDateTime &when)
{
    return QLocale::c().toString(when.toUTC(), QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'")).toLatin1();
}

DownloadResult failed(int status, const QString &error)
{
    return {DownloadResult::Status::Failed, status, 0, error};
}

} // namespace

QByteArray userAgent()
{
    return QByteArray(identity::kAppName).replace(' ', "") + '/' + identity::kVersion;
}

bool looksLikeHtml(const QByteArray &contentType, QByteArrayView body)
{
    if (contentType.trimmed().toLower().startsWith("text/html"))
        return true;
    QByteArrayView head = body.first(std::min<qsizetype>(body.size(), 512));
    if (head.startsWith("\xEF\xBB\xBF"))
        head = head.sliced(3);
    head = head.trimmed();
    const QByteArray lower = head.first(std::min<qsizetype>(head.size(), 15)).toByteArray().toLower();
    return lower.startsWith("<!doctype html") || lower.startsWith("<html");
}

DownloadResult downloadIfChanged(const QUrl &url, const QString &dest, DownloadOptions options)
{
    if (!url.isValid() || (url.scheme() != u"http" && url.scheme() != u"https"))
        return failed(0, QStringLiteral("Not a web address: %1").arg(url.toString()));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setTransferTimeout(options.timeoutMs);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    const QFileInfo cached(dest);
    if (cached.exists()) {
        request.setRawHeader("If-Modified-Since", httpDate(cached.lastModified(QTimeZone::UTC)));
        QFile etag(etagPath(dest));
        if (etag.open(QIODevice::ReadOnly))
            if (const QByteArray tag = etag.readAll().trimmed(); !tag.isEmpty())
                request.setRawHeader("If-None-Match", tag);
    }

    QNetworkAccessManager nam;
    QNetworkReply *reply = nam.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer cancelPoll;
    if (options.cancel) {
        QObject::connect(&cancelPoll, &QTimer::timeout, reply, [&] {
            if (options.cancel->load())
                reply->abort();
        });
        cancelPoll.start(100);
    }
    if (!reply->isFinished())
        loop.exec();
    cancelPoll.stop();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (options.cancel && options.cancel->load())
        return {DownloadResult::Status::Cancelled, status, 0, QString()};
    if (status == 304)
        return {DownloadResult::Status::Unchanged, status, 0, QString()};
    if (reply->error() != QNetworkReply::NoError) {
        if (status >= 400)
            return failed(status, QStringLiteral("HTTP %1 downloading %2").arg(status).arg(url.toString()));
        return failed(status, reply->errorString());
    }
    if (status != 200)
        return failed(status, QStringLiteral("HTTP %1 downloading %2").arg(status).arg(url.toString()));

    const QByteArray body = reply->readAll();
    if (looksLikeHtml(reply->header(QNetworkRequest::ContentTypeHeader).toByteArray(), body))
        return failed(status, QStringLiteral("%1 returned a web page, not a file. Use the raw file address.")
                                  .arg(url.toString()));

    QDir().mkpath(cached.absolutePath());
    QSaveFile file(dest);
    if (!file.open(QIODevice::WriteOnly) || file.write(body) != body.size() || !file.commit())
        return failed(status, file.errorString());

    const QByteArray tag = reply->rawHeader("ETag");
    if (tag.isEmpty()) {
        QFile::remove(etagPath(dest));
    } else {
        QSaveFile etag(etagPath(dest));
        if (etag.open(QIODevice::WriteOnly)) {
            etag.write(tag);
            etag.commit();
        }
    }
    return {DownloadResult::Status::Downloaded, status, body.size(), QString()};
}

} // namespace core::net
