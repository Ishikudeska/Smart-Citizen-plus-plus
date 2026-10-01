#include "core/Settings.h"

#include "core/AppIdentity.h"
#include "core/model/Enhancements.h"

#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QStandardPaths>

namespace core {

namespace {

namespace key {
const QString kScInstallRoot = QStringLiteral("sc_install_root");
const QString kActiveChannel = QStringLiteral("active_channel");
const QString kSelectedLanguage = QStringLiteral("selected_language");
const QString kUserDataDir = QStringLiteral("user_data_dir");
const QString kCacheDir = QStringLiteral("cache_dir");
const QString kFavoritePrefix = QStringLiteral("favorite_prefix");
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

QStringList Settings::enabledEnhancementFileIds() const
{
    QStringList ids;
    for (const auto &c : enhancements::categories())
        if (enhancementCategoryEnabled(QString::fromLatin1(c.id)))
            ids << c.fileIds;
    return ids;
}

} // namespace core
