#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <atomic>

// Fetching remote INI sources (a language's global.ini, a custom source's
// URL). Conditional GET from the cached copy's timestamp and ETag, so an
// unchanged file costs one round trip; the file is replaced atomically, and
// an HTML page (a github.com /blob/ link instead of a raw one) is refused
// rather than saved as an INI. Ports updater.py.
namespace core::net {

struct DownloadOptions
{
    int timeoutMs = 60'000; // per transfer stall, as the Python's socket timeout
    const std::atomic<bool> *cancel = nullptr;
};

struct DownloadResult
{
    enum class Status { Downloaded, Unchanged, Failed, Cancelled };
    Status status = Status::Failed;
    int httpStatus = 0;
    qint64 bytes = 0;
    QString error;

    bool ok() const { return status == Status::Downloaded || status == Status::Unchanged; }
};

// "<App>/<version>": some hosts reject requests without a real User-Agent.
QByteArray userAgent();

// True when `body` (or its Content-Type) is a web page, not a data file.
bool looksLikeHtml(const QByteArray &contentType, QByteArrayView body);

// Blocking: runs its own event loop, so call it from a worker thread (any
// thread works). `dest`'s ETag is kept in "<dest>.etag".
DownloadResult downloadIfChanged(const QUrl &url, const QString &dest, const DownloadOptions &options = {});

} // namespace core::net
