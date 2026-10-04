#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QString>
#include <QUrl>

#include <array>
#include <atomic>
#include <functional>
#include <optional>

// The in-app update check: GitHub's public /releases/latest for the
// configured repository (identity::kUpdateRepo), compared with this build's
// version, and a download of the release's installer. Ports app_updater.py.
namespace core::net {

using Version = std::array<int, 3>;

// "1.2.3" or "v1.2" (missing parts are 0); nothing when it does not parse,
// which callers treat as "cannot compare", never as "equal".
std::optional<Version> parseVersion(const QString &text);

// True only when both parse and `latest` is strictly newer.
bool isNewer(const QString &latest, const QString &current);

struct InstallerAsset
{
    QUrl url;
    qint64 size = 0;
    QByteArray sha256; // lower-case hex, from the asset's "digest"
};

// The release asset named "<exe>-<anything>-Setup.exe" (case-insensitive,
// whole name), so portable zips and checksums are never picked. Only an
// https asset carrying GitHub's "sha256:<hex>" digest qualifies: the
// installer runs silently, so it is never run unverified.
std::optional<InstallerAsset> pickInstallerAsset(const QJsonArray &assets, const QString &exeName);

struct UpdateCheck
{
    enum class Status { UpToDate, Available, Failed, Disabled };
    Status status = Status::Failed;
    QString latest;       // without a leading "v"
    QUrl releasePage;
    QString notes;        // the release body (Markdown)
    std::optional<InstallerAsset> installer;
    QString error;
};

// The API address for a "owner/repo"; empty for an empty repo.
QUrl latestReleaseApi(const QString &repo);

// Blocking (own event loop): call from a worker thread. Disabled when no
// repository is configured.
UpdateCheck checkForUpdate(const QString &repo, const QString &currentVersion, int timeoutMs = 10'000);

// Streams the installer into `destDir` through a ".part" file renamed on
// completion, so a half download is never mistaken for an installer. The
// file must match the asset's size (when known) and SHA-256, or it is
// deleted. Returns the installer's path, or an error message in `error`.
using DownloadProgress = std::function<void(qint64 done, qint64 total)>;
QString downloadInstaller(const InstallerAsset &asset, const QString &destDir, QString *error,
                          const DownloadProgress &progress = {}, const std::atomic<bool> *cancel = nullptr);

} // namespace core::net
