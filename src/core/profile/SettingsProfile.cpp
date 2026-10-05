#include "core/profile/SettingsProfile.h"

#include "core/AppIdentity.h"
#include "core/EnginePaths.h"
#include "engine/p4k/Archive.h"
#include "engine/zip/ZipWriter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace core::profile {

namespace {

constexpr QLatin1StringView kManifestName("manifest.json");
constexpr QLatin1StringView kSettingsName("settings.json");
constexpr QLatin1StringView kOverridesPrefix("overrides/");
constexpr QLatin1StringView kUserIniSuffix("/user.ini");
constexpr QLatin1StringView kKind("settings-backup");
// Backups made by Smart Citizen carry this marker; they share our keys.
constexpr QLatin1StringView kSmartCitizenMarker("SmartCitizen");

QString appMarker()
{
    return QString::fromUtf8(identity::kAppName).remove(u' ');
}

std::string utf8(const QString &s)
{
    return s.toStdString();
}

// One plain path segment: no separators, no "." or "..", no drive.
bool isSafeChannel(const QString &channel)
{
    if (channel.isEmpty() || channel == u"." || channel == u"..")
        return false;
    if (channel.contains(u'/') || channel.contains(u'\\'))
        return false;
    return !(channel.size() >= 2 && channel[1] == u':'); // ntpath.splitdrive
}

QJsonDocument parseJson(const std::vector<std::uint8_t> &bytes, QString *error)
{
    QJsonParseError e;
    const QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size())), &e);
    if (e.error != QJsonParseError::NoError)
        *error = e.errorString();
    return doc;
}

} // namespace

QString defaultBackupFilename(QDate today)
{
    return QStringLiteral("%1-Settings-Backup-%2.zip")
        .arg(appMarker(), today.toString(QStringLiteral("yyyyMMdd")));
}

std::expected<int, QString> writeProfileZip(const QString &zipPath, const QVariantMap &settings,
                                            const QMap<QString, QString> &overrides,
                                            const QString &appVersion, const QString &sourceMode,
                                            const QDateTime &now)
{
    QJsonObject manifest{
        {QStringLiteral("app"), appMarker()},
        {QStringLiteral("kind"), kKind},
        {QStringLiteral("schema_version"), kSchemaVersion},
        {QStringLiteral("app_version"), appVersion},
        {QStringLiteral("exported_at"), now.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"))},
        {QStringLiteral("source_mode"), sourceMode},
        {QStringLiteral("channels"), QJsonArray::fromStringList(overrides.keys())},
    };

    auto zip = engine::zip::ZipWriter::create(fsPath(zipPath));
    if (!zip)
        return std::unexpected(errorText(zip.error()));
    const std::time_t stamp = now.toSecsSinceEpoch();
    const auto add = [&](const QString &name, const QByteArray &bytes) -> std::expected<void, QString> {
        if (auto r = zip->add(utf8(name), std::string_view(bytes.constData(), bytes.size()), 9, stamp); !r)
            return std::unexpected(errorText(r.error()));
        return {};
    };

    int entries = 0;
    if (auto r = add(kManifestName, QJsonDocument(manifest).toJson()); !r)
        return std::unexpected(r.error());
    ++entries;
    if (auto r = add(kSettingsName, QJsonDocument(QJsonObject::fromVariantMap(settings)).toJson()); !r)
        return std::unexpected(r.error());
    ++entries;
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it) {
        // Forward slashes inside the zip, whatever the host.
        if (auto r = add(kOverridesPrefix + it.key() + kUserIniSuffix, it.value().toUtf8()); !r)
            return std::unexpected(r.error());
        ++entries;
    }
    if (auto r = zip->finish(); !r)
        return std::unexpected(errorText(r.error()));
    return entries;
}

std::expected<ProfileContents, QString> readProfileZip(const QString &zipPath)
{
    const auto archive = engine::p4k::Archive::open(fsPath(zipPath));
    if (!archive)
        return std::unexpected(QStringLiteral("Not a readable zip file: %1").arg(errorText(archive.error())));
    const engine::p4k::Archive &zip = **archive;

    const auto read = [&](const QString &name) -> std::optional<std::vector<std::uint8_t>> {
        const auto index = zip.find(utf8(name));
        if (!index || QString::fromUtf8(zip.name(*index)) != name)
            return std::nullopt;
        auto bytes = zip.read(*index);
        if (!bytes)
            return std::nullopt;
        return std::move(*bytes);
    };

    const auto manifestBytes = read(kManifestName);
    if (!manifestBytes)
        return std::unexpected(QStringLiteral("This zip has no manifest, so it isn't a settings backup."));
    QString error;
    const QJsonDocument manifestDoc = parseJson(*manifestBytes, &error);
    if (!error.isEmpty())
        return std::unexpected(QStringLiteral("Backup manifest is unreadable: %1").arg(error));
    const QJsonObject manifest = manifestDoc.object();
    const QString marker = manifest.value(QStringLiteral("app")).toString();
    if (!manifestDoc.isObject() || (marker != appMarker() && marker != kSmartCitizenMarker))
        return std::unexpected(QStringLiteral("This backup wasn't made by %1 or Smart Citizen.")
                                   .arg(QString::fromUtf8(identity::kAppName)));

    const QJsonValue schemaValue = manifest.value(QStringLiteral("schema_version"));
    // An int, as json.loads would give: not a bool, not a fraction.
    if (!schemaValue.isDouble() || schemaValue.toDouble() != double(schemaValue.toInteger()))
        return std::unexpected(QStringLiteral("Backup manifest is missing a schema version."));
    const int schema = int(schemaValue.toInteger());
    if (schema > kSchemaVersion)
        return std::unexpected(
            QStringLiteral("This backup was made by a newer version (backup schema %1, this build "
                           "supports %2). Update, then import again.")
                .arg(schema)
                .arg(kSchemaVersion));

    ProfileContents contents;
    contents.schemaVersion = schema;
    contents.appVersion = manifest.value(QStringLiteral("app_version")).toVariant().toString();
    contents.exportedAt = manifest.value(QStringLiteral("exported_at")).toVariant().toString();
    contents.sourceMode = manifest.value(QStringLiteral("source_mode")).toVariant().toString();

    // settings.json may be absent, but not corrupt.
    if (const auto settingsBytes = read(kSettingsName)) {
        const QJsonDocument doc = parseJson(*settingsBytes, &error);
        if (!error.isEmpty())
            return std::unexpected(QStringLiteral("Backup settings are unreadable: %1").arg(error));
        if (!doc.isObject())
            return std::unexpected(QStringLiteral("Backup settings.json is not a JSON object."));
        contents.settings = doc.object().toVariantMap();
    }

    for (std::size_t i = 0; i < zip.entryCount(); ++i) {
        const QString name = QString::fromUtf8(zip.name(i));
        if (!name.startsWith(kOverridesPrefix) || !name.endsWith(kUserIniSuffix) ||
            name.size() <= kOverridesPrefix.size() + kUserIniSuffix.size())
            continue;
        const QString channel = name.sliced(kOverridesPrefix.size()).chopped(kUserIniSuffix.size());
        if (!isSafeChannel(channel))
            continue;
        const auto bytes = zip.read(i);
        if (!bytes)
            continue;
        // bytes.decode("utf-8"): strict, and a BOM stays part of the text.
        const QByteArrayView view(bytes->data(), qsizetype(bytes->size()));
        if (view.isValidUtf8())
            contents.overrides.insert(channel, QString::fromUtf8(view));
    }
    return contents;
}

bool isProfileExcludedKey(const QString &key, const QVariant &value)
{
    static const QSet<QString> excluded = {
        QStringLiteral("user_data_dir"),
        QStringLiteral("UserDataDir"),
        QStringLiteral("cache_dir"),
        QStringLiteral("pending_cache_cleanup"),
        QStringLiteral("window_geometry"),
        QStringLiteral("window_state"),
        QStringLiteral("string_column_widths"),
        QStringLiteral("base_global_path"),
        QStringLiteral("vehicles_path"),
        QStringLiteral("last_overrides_path"),
        QStringLiteral("post_import/apply_pending"),
    };
    if (excluded.contains(key) || key.startsWith(u'_'))
        return true;
    if (key.startsWith(QStringLiteral("data_sources/")) && key.endsWith(QStringLiteral("/path"))) {
        const QString text = value.toString().toLower();
        return !text.startsWith(QStringLiteral("http://")) && !text.startsWith(QStringLiteral("https://"));
    }
    return false;
}

QVariantMap exportSettingsValues(const Settings &settings)
{
    QVariantMap out;
    for (const auto keys = settings.allKeys(); const QString &key : keys) {
        const QVariant value = settings.value(key);
        if (!isProfileExcludedKey(key, value))
            out.insert(key, value);
    }
    return out;
}

int importSettingsValues(Settings &settings, const QVariantMap &values)
{
    int applied = 0;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (it.key().isEmpty() || isProfileExcludedKey(it.key(), it.value()))
            continue;
        settings.setValue(it.key(), it.value());
        ++applied;
    }
    settings.sync();
    return applied;
}

InstallPathOutcome reconcileImportedInstallPath(Settings &settings, const std::function<QString()> &detect)
{
    const QString imported = settings.scInstallRoot();
    if (!imported.isEmpty() && isScInstallRoot(imported)) {
        settings.setScInstallRoot(imported);
        return InstallPathOutcome::Restored;
    }
    settings.remove(QStringLiteral("sc_install_root"));
    settings.remove(QStringLiteral("game_install_path")); // Smart Citizen's legacy key
    const QString detected = detect ? detect() : QString();
    if (detected.isEmpty())
        return InstallPathOutcome::None;
    settings.setScInstallRoot(detected);
    return InstallPathOutcome::Redetected;
}

} // namespace core::profile
