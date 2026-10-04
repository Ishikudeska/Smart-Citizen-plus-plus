#pragma once

#include "core/ScInstall.h"
#include "core/Settings.h"

#include <QDate>
#include <QDateTime>
#include <QMap>
#include <QString>
#include <QVariantMap>

#include <expected>
#include <functional>

// Settings backups: one small zip with everything that makes an install the
// user's own, to move to another PC or restore after a fresh portable unzip.
//
//   manifest.json         app marker, schema, app version, time, mode, channels
//   settings.json         every setting except machine-local ones
//   overrides/<channel>/user.ini
//
// Caches and backups are left out (they regenerate or are history). Reads
// Smart Citizen's own backups too, so its users can bring their setup
// over. Ports settings_profile.py and AppSettings' export/import helpers.
namespace core::profile {

// Raised when the on-disk layout changes in a way older readers can't take;
// a newer backup is refused rather than half-restored.
inline constexpr int kSchemaVersion = 1;

inline const QString kSourcePortable = QStringLiteral("portable");
inline const QString kSourceInstalled = QStringLiteral("registry"); // Smart Citizen's name for it

struct ProfileContents
{
    QVariantMap settings;
    QMap<QString, QString> overrides; // channel -> user.ini text
    int schemaVersion = kSchemaVersion;
    QString appVersion;
    QString exportedAt;
    QString sourceMode;
};

// "<App>-Settings-Backup-YYYYMMDD.zip": no version, since backups move
// between versions.
QString defaultBackupFilename(QDate today = QDate::currentDate());

// Returns the number of zip entries written.
std::expected<int, QString> writeProfileZip(const QString &zipPath, const QVariantMap &settings,
                                            const QMap<QString, QString> &overrides, const QString &appVersion,
                                            const QString &sourceMode,
                                            const QDateTime &now = QDateTime::currentDateTime());

// Validates the manifest (an app marker this build knows, a schema it
// supports) and reads the settings and per-channel overrides. An override
// whose channel is not one plain path segment is ignored: the zip is
// untrusted and the channel becomes a folder name.
std::expected<ProfileContents, QString> readProfileZip(const QString &zipPath);

// Keys that stay on this machine: install/data/cache paths, window and
// column layout, "_"-prefixed migration markers, and data-source paths
// that are local files rather than URLs.
bool isProfileExcludedKey(const QString &key, const QVariant &value = {});

QVariantMap exportSettingsValues(const Settings &settings);
// Layers `values` over the current settings (excluded keys skipped);
// returns the number applied.
int importSettingsValues(Settings &settings, const QVariantMap &values);

enum class InstallPathOutcome {
    Restored,   // the backup's install path is valid here
    Redetected, // it was not; another install was found
    None,       // nothing usable on this machine
};
// A backup from another PC can name an install that does not exist here:
// keep it only when it is still a real install, else detect afresh.
InstallPathOutcome reconcileImportedInstallPath(Settings &settings,
                                                const std::function<QString()> &detect = locateScInstall);

} // namespace core::profile
