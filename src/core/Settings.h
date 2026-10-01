#pragma once

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

    // Install and channel.
    QString scInstallRoot() const;
    void setScInstallRoot(const QString &path);
    QString activeChannel() const; // falls back to LIVE for unknown values
    void setActiveChannel(const QString &channel);

    QString selectedLanguage() const;
    void setSelectedLanguage(const QString &language);

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

private:
    std::unique_ptr<QSettings> settings_;
};

} // namespace core
