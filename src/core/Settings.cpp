#include "core/Settings.h"

#include "core/AppIdentity.h"
#include "core/model/Enhancements.h"
#include "core/text/PyJson.h"
#include "core/text/PyText.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimeZone>

#include <algorithm>

namespace core {

namespace {

namespace key {
constexpr QLatin1StringView kScInstallRoot("sc_install_root");
constexpr QLatin1StringView kActiveChannel("active_channel");
constexpr QLatin1StringView kSelectedLanguage("selected_language");
constexpr QLatin1StringView kUserDataDir("user_data_dir");
constexpr QLatin1StringView kCacheDir("cache_dir");
constexpr QLatin1StringView kFavoritePrefix("favorite_prefix");
constexpr QLatin1StringView kOwnedItems("owned_items");
QString watermark(const QString &channel)
{
    return QStringLiteral("blueprint_log_watermark/") + channel;
}
QString enhancementCategory(const QString &id)
{
    return QStringLiteral("enhancements/categories/%1/enabled").arg(id);
}
} // namespace key

} // namespace

QStringList channels()
{
    return {QStringLiteral("LIVE"), QStringLiteral("PTU"), QStringLiteral("EPTU"), QStringLiteral("HOTFIX"),
            QStringLiteral("TECH-PREVIEW")};
}

QString scLanguageId(const QString &language)
{
    static const QHash<QString, QString> ids = {
        {QStringLiteral("english"), QStringLiteral("english")},
        {QStringLiteral("french"), QStringLiteral("french_(france)")},
        {QStringLiteral("spanish"), QStringLiteral("spanish_(spain)")},
        {QStringLiteral("portuguese_br"), QStringLiteral("portuguese_(brazil)")},
        {QStringLiteral("japanese"), QStringLiteral("japanese_(japan)")},
        {QStringLiteral("chinese"), QStringLiteral("chinese_(simplified)")},
        {QStringLiteral("italian"), QStringLiteral("italian_(italy)")},
        {QStringLiteral("german"), QStringLiteral("german_(germany)")},
    };
    return ids.value(language, language);
}

QString Settings::defaultFilePath()
{
    // A separate profile for development and screenshots.
    if (const QString file = qEnvironmentVariable("SCX_SETTINGS_FILE"); !file.isEmpty())
        return file;
    if (identity::kPortable)
        return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data/settings.ini"));
#ifdef Q_OS_WIN
    const QString roaming = qEnvironmentVariable("APPDATA");
    if (!roaming.isEmpty())
        return QDir(roaming).filePath(QString::fromLatin1(identity::kOrgName) + QStringLiteral("/settings.ini"));
#endif
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
        .filePath(QStringLiteral("settings.ini"));
}

Settings::Settings()
    : Settings(defaultFilePath())
{
}

Settings::Settings(const QString &iniPath)
    : settings_(std::make_unique<QSettings>(iniPath, QSettings::IniFormat))
{
}

QString Settings::filePath() const
{
    return settings_->fileName();
}

void Settings::sync()
{
    settings_->sync();
}

QVariant Settings::value(const QString &k, const QVariant &fallback) const
{
    return settings_->value(k, fallback);
}

void Settings::setValue(const QString &k, const QVariant &v)
{
    settings_->setValue(k, v);
}

void Settings::remove(const QString &k)
{
    settings_->remove(k);
}

QStringList Settings::allKeys() const
{
    return settings_->allKeys();
}

QString Settings::scInstallRoot() const
{
    return settings_->value(key::kScInstallRoot).toString();
}

void Settings::setScInstallRoot(const QString &path)
{
    settings_->setValue(key::kScInstallRoot, path);
}

QString Settings::activeChannel() const
{
    const QString value = settings_->value(key::kActiveChannel, kDefaultChannel).toString();
    return channels().contains(value) ? value : kDefaultChannel;
}

void Settings::setActiveChannel(const QString &channel)
{
    if (channels().contains(channel))
        settings_->setValue(key::kActiveChannel, channel);
}

QString Settings::selectedLanguage() const
{
    return settings_->value(key::kSelectedLanguage, kDefaultLanguage).toString();
}

void Settings::setSelectedLanguage(const QString &language)
{
    settings_->setValue(key::kSelectedLanguage, language);
}

QString Settings::languageSourceOverride(const QString &language) const
{
    return settings_->value(QStringLiteral("language_source_url/") + language).toString();
}

void Settings::setLanguageSourceOverride(const QString &language, const QString &url)
{
    const QString key = QStringLiteral("language_source_url/") + language;
    if (url.isEmpty())
        settings_->remove(key);
    else
        settings_->setValue(key, url);
}

QString languageBaseUrl(const Settings &settings, const QString &language, const QString &languagesDir)
{
    if (const QString url = settings.languageSourceOverride(language); !url.isEmpty())
        return url;
    QFile file(QDir(languagesDir).filePath(QStringLiteral("sources.json")));
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    return QJsonDocument::fromJson(file.readAll()).object().value(language).toString();
}

QString Settings::userDataDirOverride() const
{
    return settings_->value(key::kUserDataDir).toString();
}

void Settings::setUserDataDirOverride(const QString &path)
{
    if (path.isEmpty())
        settings_->remove(key::kUserDataDir);
    else
        settings_->setValue(key::kUserDataDir, path);
}

QString Settings::cacheDirOverride() const
{
    return settings_->value(key::kCacheDir).toString();
}

void Settings::setCacheDirOverride(const QString &path)
{
    if (path.isEmpty())
        settings_->remove(key::kCacheDir);
    else
        settings_->setValue(key::kCacheDir, path);
}

QString Settings::favoritePrefix() const
{
    return settings_->value(key::kFavoritePrefix, QStringLiteral("*")).toString();
}

void Settings::setFavoritePrefix(const QString &prefix)
{
    settings_->setValue(key::kFavoritePrefix, prefix);
}

bool Settings::enhancementCategoryEnabled(const QString &categoryId) const
{
    return settings_->value(key::enhancementCategory(categoryId), true).toBool();
}

void Settings::setEnhancementCategoryEnabled(const QString &categoryId, bool enabled)
{
    settings_->setValue(key::enhancementCategory(categoryId), enabled);
}

tags::TagConfig Settings::tagConfig(const QString &category)
{
    const QString key = QStringLiteral("tag_builder/%1/config").arg(category);
    const QString raw = settings_->value(key).toString();
    if (raw.isEmpty())
        return tags::defaultConfig(category);
    auto config = tags::TagConfig::fromJson(raw);
    if (!config)
        return tags::defaultConfig(category);
    tags::migrateMapping(category, *config);
    tags::backfillNewElements(category, *config);

    // 1.5.0 gave commodities a second flag; an old single-flag config with
    // separator "none" would now mash them together ("[CFCollection]").
    // Upgrade once, so a later deliberate "none" sticks.
    if (category == u"commodities") {
        const QString marker = QStringLiteral("tag_builder/commodities/sep_migrated");
        if (!settings_->value(marker, false).toBool()) {
            if (config->separator == u"none") {
                config->separator = QStringLiteral("pipe");
                settings_->setValue(key, config->toJson());
            }
            settings_->setValue(marker, true);
        }
    }
    return *config;
}

void Settings::setTagConfig(const QString &category, const tags::TagConfig &config)
{
    settings_->setValue(QStringLiteral("tag_builder/%1/config").arg(category), config.toJson());
}

QMap<QString, tags::TagConfig> Settings::allTagConfigs()
{
    QMap<QString, tags::TagConfig> all;
    for (const QString &category : tags::kCategories)
        all.insert(category, tagConfig(category));
    return all;
}

bool Settings::annotateMissionDescs() const
{
    return settings_->value(QStringLiteral("tag_builder/annotate_mission_descs"), true).toBool();
}

void Settings::setAnnotateMissionDescs(bool enabled)
{
    settings_->setValue(QStringLiteral("tag_builder/annotate_mission_descs"), enabled);
}

QString Settings::missionHeader(const QString &key) const
{
    static const QHash<QString, QString> defaults = {
        {QStringLiteral("details"), QStringLiteral("MISSION DETAILS")},
        {QStringLiteral("blueprints"), QStringLiteral("POTENTIAL BLUEPRINTS")},
        {QStringLiteral("items"), QStringLiteral("ITEM REWARDS")},
        {QStringLiteral("blueprint_data"), QStringLiteral("BLUEPRINT DATA")},
    };
    if (!defaults.contains(key))
        return QString();
    return settings_->value(QStringLiteral("mission_header/") + key, defaults.value(key)).toString();
}

void Settings::setMissionHeader(const QString &key, const QString &text)
{
    if (!missionHeader(key).isNull())
        settings_->setValue(QStringLiteral("mission_header/") + key, text);
}

QString Settings::repXpLabel() const
{
    return settings_->value(QStringLiteral("rep_xp_label"), QStringLiteral("Rep")).toString();
}

void Settings::setRepXpLabel(const QString &label)
{
    settings_->setValue(QStringLiteral("rep_xp_label"), label);
}

QString Settings::missionHeaderEmTag() const
{
    // EM1/EM2 never rendered in-game; a stored one reads as the default.
    const QString tag = settings_->value(QStringLiteral("mission_header/em_tag")).toString();
    return tag == u"EM3" || tag == u"EM4" ? tag : QStringLiteral("EM3");
}

void Settings::setMissionHeaderEmTag(const QString &tag)
{
    settings_->setValue(QStringLiteral("mission_header/em_tag"), tag);
}

bool Settings::missionDetailField(const QString &field) const
{
    return settings_->value(QStringLiteral("mission_field/") + field, true).toBool();
}

void Settings::setMissionDetailField(const QString &field, bool enabled)
{
    if (kMissionFieldKeys.contains(field))
        settings_->setValue(QStringLiteral("mission_field/") + field, enabled);
}

bool Settings::missionTitleTagDefault(const QString &field)
{
    return field != u"rep_track";
}

bool Settings::missionTitleTag(const QString &field) const
{
    return settings_->value(QStringLiteral("mission_title_tag/") + field, missionTitleTagDefault(field)).toBool();
}

void Settings::setMissionTitleTag(const QString &field, bool enabled)
{
    if (kMissionTitleTagKeys.contains(field))
        settings_->setValue(QStringLiteral("mission_title_tag/") + field, enabled);
}

void Settings::migrateTitleTagSettings()
{
    const QString marker = QStringLiteral("mission_title_tag/migrated");
    if (settings_->value(marker, false).toBool())
        return;
    // Only an explicit earlier "off" carries forward.
    settings_->setValue(QStringLiteral("mission_title_tag/blueprint"),
                        settings_->value(QStringLiteral("mission_field/blueprint_tag"), true).toBool());
    settings_->setValue(QStringLiteral("mission_title_tag/ace"),
                        settings_->value(QStringLiteral("mission_field/ace"), true).toBool());
    settings_->setValue(marker, true);
}

bool Settings::statsPrepend() const
{
    return settings_->value(QStringLiteral("stats_prepend"), false).toBool();
}

void Settings::setStatsPrepend(bool enabled)
{
    settings_->setValue(QStringLiteral("stats_prepend"), enabled);
}

bool Settings::standardizeEarnableShipNames() const
{
    return settings_->value(QStringLiteral("standardize_earnable_ship_names"), false).toBool();
}

void Settings::setStandardizeEarnableShipNames(bool enabled)
{
    settings_->setValue(QStringLiteral("standardize_earnable_ship_names"), enabled);
}

bool Settings::rsOreNameAnnotations() const
{
    return settings_->value(QStringLiteral("enhancements/rs_ore_name_annotations"), true).toBool();
}

void Settings::setRsOreNameAnnotations(bool enabled)
{
    settings_->setValue(QStringLiteral("enhancements/rs_ore_name_annotations"), enabled);
}

QString Settings::theme() const
{
    // Anything else, including the retired "scle" and "odw", reads as dark.
    const QString t = settings_->value(QStringLiteral("theme")).toString();
    return t == u"light" ? t : QStringLiteral("dark");
}

void Settings::setTheme(const QString &theme)
{
    settings_->setValue(QStringLiteral("theme"), theme);
}

QString Settings::uiMode() const
{
    return settings_->value(QStringLiteral("ui_mode")).toString() == u"simple" ? QStringLiteral("simple")
                                                                              : QStringLiteral("advanced");
}

void Settings::setUiMode(const QString &mode)
{
    settings_->setValue(QStringLiteral("ui_mode"), mode);
}

bool Settings::includeNewLines() const
{
    return settings_->value(QStringLiteral("include_new_lines"), false).toBool();
}

void Settings::setIncludeNewLines(bool enabled)
{
    settings_->setValue(QStringLiteral("include_new_lines"), enabled);
}

bool Settings::oneDriveWarningDismissed() const
{
    return settings_->value(QStringLiteral("onedrive_warning_dismissed"), false).toBool();
}

void Settings::setOneDriveWarningDismissed(bool dismissed)
{
    settings_->setValue(QStringLiteral("onedrive_warning_dismissed"), dismissed);
}

QString Settings::tutorialCompletedVersion() const
{
    return settings_->value(QStringLiteral("tutorial_completed_version")).toString();
}

void Settings::setTutorialCompletedVersion(const QString &version)
{
    settings_->setValue(QStringLiteral("tutorial_completed_version"), version);
}

bool Settings::tutorialDisabled() const
{
    return settings_->value(QStringLiteral("tutorial_disabled"), false).toBool();
}

void Settings::setTutorialDisabled(bool disabled)
{
    settings_->setValue(QStringLiteral("tutorial_disabled"), disabled);
}

QSet<QString> Settings::ownedItems() const
{
    const QByteArray raw = settings_->value(key::kOwnedItems).toString().toUtf8();
    QSet<QString> names;
    for (const auto values = QJsonDocument::fromJson(raw).array(); const QJsonValue &v : values)
        if (v.isString())
            names.insert(v.toString());
    return names;
}

void Settings::setOwnedItems(const QSet<QString> &names)
{
    // json.dumps(sorted(names)), so the stored text matches Smart Citizen's.
    QStringList sorted(names.cbegin(), names.cend());
    std::sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b) { return py::less(a, b); });
    QStringList items;
    for (const QString &name : sorted)
        items << py::jsonString(name, true);
    settings_->setValue(key::kOwnedItems, u'[' + items.join(QStringLiteral(", ")) + u']');
}

bool Settings::toggleOwnedItem(const QString &name)
{
    QSet<QString> owned = ownedItems();
    const bool nowOwned = !owned.contains(name);
    if (nowOwned)
        owned.insert(name);
    else
        owned.remove(name);
    setOwnedItems(owned);
    return nowOwned;
}

bool Settings::blueprintShowTags() const
{
    return settings_->value(QStringLiteral("blueprints/show_tags"), false).toBool();
}

void Settings::setBlueprintShowTags(bool enabled)
{
    settings_->setValue(QStringLiteral("blueprints/show_tags"), enabled);
}

bool Settings::scanOtherChannels() const
{
    return settings_->value(QStringLiteral("blueprints/scan_other_channels"), true).toBool();
}

void Settings::setScanOtherChannels(bool enabled)
{
    settings_->setValue(QStringLiteral("blueprints/scan_other_channels"), enabled);
}

QDateTime Settings::blueprintLogWatermark(const QString &channel) const
{
    const QString raw = settings_->value(key::watermark(channel.isEmpty() ? activeChannel() : channel)).toString();
    // datetime.isoformat(): "2026-03-26T17:15:41.684000+00:00".
    static const QRegularExpression iso(QStringLiteral(
        R"(^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.(\d{1,6}))?(Z|[+-]\d{2}:\d{2})?$)"));
    const QRegularExpressionMatch m = iso.match(raw);
    if (!m.hasMatch())
        return {};
    const auto n = [&](int group) { return m.capturedView(group).toInt(); };
    const int ms = m.hasCaptured(7) ? (m.captured(7) + QStringLiteral("00")).first(3).toInt() : 0;
    const QDate date(n(1), n(2), n(3));
    const QTime time(n(4), n(5), n(6), ms);
    if (!date.isValid() || !time.isValid())
        return {};
    int offset = 0;
    if (const QString tz = m.captured(8); tz.size() == 6)
        offset = (tz.sliced(1, 2).toInt() * 3600 + tz.sliced(4, 2).toInt() * 60) * (tz[0] == u'-' ? -1 : 1);
    return QDateTime(date, time, QTimeZone::fromSecondsAheadOfUtc(offset)).toUTC();
}

void Settings::setBlueprintLogWatermark(const QDateTime &when, const QString &channel)
{
    const QDateTime utc = when.toUTC();
    QString text = utc.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
    if (const int ms = utc.time().msec(); ms != 0)
        text += u'.' + QStringLiteral("%1").arg(ms, 3, 10, QChar(u'0')) + QStringLiteral("000");
    settings_->setValue(key::watermark(channel.isEmpty() ? activeChannel() : channel), text + QStringLiteral("+00:00"));
}

QStringList Settings::enabledEnhancementFileIds() const
{
    QStringList ids;
    for (const auto &c : enhancements::categories())
        if (enhancementCategoryEnabled(QString::fromLatin1(c.id)))
            ids << c.fileIds;
    return ids;
}

} // namespace core
