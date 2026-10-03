#pragma once

#include "core/tags/TagBuilder.h"

#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <memory>

namespace core {

// Star Citizen's release channels, in display order. Each is a folder under
// the install root (LIVE\, PTU\, ...).
QStringList channels();
inline const QString kDefaultChannel = QStringLiteral("LIVE");

inline const QString kDefaultLanguage = QStringLiteral("english");
// Our language folder name -> the id SC uses for its Localization folder and
// user.cfg's g_language ("portuguese_br" -> "portuguese_(brazil)").
QString scLanguageId(const QString &language);

class Settings;

inline const QStringList kMissionFieldKeys = {
    QStringLiteral("mission_type"), QStringLiteral("difficulty"), QStringLiteral("spawns"),
    QStringLiteral("reputation"),   QStringLiteral("blueprints"), QStringLiteral("ace"),
    QStringLiteral("resource_signatures"),
};
inline const QStringList kMissionTitleTagKeys = {
    QStringLiteral("rep"), QStringLiteral("blueprint"), QStringLiteral("ace"), QStringLiteral("rs"),
    QStringLiteral("rep_track"),
};

// Where a language's global.ini downloads from: the user's override, else
// the bundled <languagesDir>/sources.json map; empty for English (it comes
// from the local Data.p4k) and for unmapped languages.
QString languageBaseUrl(const Settings &settings, const QString &language, const QString &languagesDir);

// Typed access to the app's settings file: %APPDATA%\<org>\settings.ini, or
// data\settings.ini next to the executable in portable builds. QSettings is
// thread-safe; one instance per thread is fine. Keys keep Smart Citizen's
// names where the setting is the same.
class Settings
{
public:
    Settings();                                 // the app's settings file
    explicit Settings(const QString &iniPath);  // a specific file (tests, tools)

    static QString defaultFilePath();
    QString filePath() const;
    void sync();

    // Generic access for settings without a typed accessor.
    QVariant value(const QString &key, const QVariant &fallback = {}) const;
    void setValue(const QString &key, const QVariant &value);
    void remove(const QString &key);
    QStringList allKeys() const;

    // Install and channel.
    QString scInstallRoot() const;
    void setScInstallRoot(const QString &path);
    QString activeChannel() const; // falls back to LIVE for unknown values
    void setActiveChannel(const QString &channel);

    QString selectedLanguage() const;
    void setSelectedLanguage(const QString &language);
    // A user-mapped download URL for a language's global.ini ("Map Language
    // File"); empty clears it.
    QString languageSourceOverride(const QString &language) const;
    void setLanguageSourceOverride(const QString &language, const QString &url);

    // Folder overrides; empty means the default location.
    QString userDataDirOverride() const;
    void setUserDataDirOverride(const QString &path);
    QString cacheDirOverride() const;
    void setCacheDirOverride(const QString &path);

    // Strings table.
    QString favoritePrefix() const; // default "*"
    void setFavoritePrefix(const QString &prefix);

    // Enhancements: one toggle per enhancements::categories() id, default on.
    bool enhancementCategoryEnabled(const QString &categoryId) const;
    void setEnhancementCategoryEnabled(const QString &categoryId, bool enabled);
    QStringList enabledEnhancementFileIds() const;

    // Tag Builder: one JSON blob per category under tag_builder/<cat>/config.
    // Loading falls back to the default on a missing or malformed blob and
    // applies the version upgrades (renamed damage keys, newly added
    // elements, the one-time commodities separator fix).
    tags::TagConfig tagConfig(const QString &category);
    void setTagConfig(const QString &category, const tags::TagConfig &config);
    QMap<QString, tags::TagConfig> allTagConfigs();
    bool annotateMissionDescs() const; // weave tags into POTENTIAL BLUEPRINTS lists; default on
    void setAnnotateMissionDescs(bool enabled);

    // Mission section headers the generator writes: "details",
    // "blueprints", "items", "blueprint_data". Unknown keys read empty.
    QString missionHeader(const QString &key) const;
    void setMissionHeader(const QString &key, const QString &text);
    QString repXpLabel() const; // default "Rep"
    void setRepXpLabel(const QString &label);
    QString missionHeaderEmTag() const; // "EM3" (default) or "EM4"; anything else reads as the default
    void setMissionHeaderEmTag(const QString &tag);
    // DETAILS body lines (kMissionFieldKeys), all default on; title tags
    // (kMissionTitleTagKeys), all default on except "rep_track".
    bool missionDetailField(const QString &field) const;
    void setMissionDetailField(const QString &field, bool enabled); // unknown fields are ignored
    bool missionTitleTag(const QString &field) const;
    void setMissionTitleTag(const QString &field, bool enabled);
    static bool missionTitleTagDefault(const QString &field);
    // One-time copy of the pre-2.2 blueprint_tag/ace toggles into the title tags.
    void migrateTitleTagSettings();
    bool statsPrepend() const; // stats above the description; default off
    void setStatsPrepend(bool enabled);
    bool standardizeEarnableShipNames() const; // PYX/WIK names; default off
    void setStandardizeEarnableShipNames(bool enabled);
    bool rsOreNameAnnotations() const; // " (RS ####)" on ore names; default on
    void setRsOreNameAnnotations(bool enabled);

    // Appearance and UI state.
    QString theme() const; // "dark" (default) or "light"
    void setTheme(const QString &theme);
    QString uiMode() const; // "advanced" (default) or "simple"
    void setUiMode(const QString &mode);
    bool includeNewLines() const; // apply "New" (not in stock) enhancement lines; default off
    void setIncludeNewLines(bool enabled);
    bool oneDriveWarningDismissed() const;
    void setOneDriveWarningDismissed(bool dismissed);
    QString tutorialCompletedVersion() const;
    void setTutorialCompletedVersion(const QString &version);
    bool tutorialDisabled() const;
    void setTutorialDisabled(bool disabled);

    // Blueprint Tracker. The owned set is a JSON list of names, as Smart
    // Citizen stored it.
    QSet<QString> ownedItems() const;
    void setOwnedItems(const QSet<QString> &names);
    bool toggleOwnedItem(const QString &name); // the new owned state
    bool blueprintShowTags() const; // default off
    void setBlueprintShowTags(bool enabled);
    bool scanOtherChannels() const; // also scan LIVE/HOTFIX's partner; default on
    void setScanOtherChannels(bool enabled);
    // Newest "Received Blueprint" event a scan consumed, per channel (the
    // active one when `channel` is empty); invalid when unset or unreadable.
    QDateTime blueprintLogWatermark(const QString &channel = {}) const;
    void setBlueprintLogWatermark(const QDateTime &when, const QString &channel = {});

private:
    std::unique_ptr<QSettings> settings_;
};

} // namespace core
