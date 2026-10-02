#include "ConfigController.h"

#include "AppController.h"

#include "core/i18n/Translator.h"
#include "core/merge/SourceLoader.h"
#include "core/net/Downloader.h"
#include "core/profile/SettingsProfile.h"
#include "core/text/IniFile.h"
#include "core/user/UserIni.h"
#include "core/util/OneDrive.h"

#include <QCoreApplication>

#include <algorithm>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTemporaryFile>

Q_DECLARE_LOGGING_CATEGORY(lcApp)

using namespace core;

namespace {

// QObject::tr would shadow the catalogue lookup inside members.
QString text(const char *key, const QVariantHash &args = {})
{
    return core::i18n::tr(key, args);
}

QString thousands(qint64 n)
{
    return QLocale(QLocale::English).toString(n);
}

} // namespace

ConfigController::ConfigController(QObject *parent) : QObject(parent)
{
    connect(&app(), &AppController::pathsChanged, this, &ConfigController::changed);
}

AppController &ConfigController::app() const
{
    return *AppController::instance();
}

QString ConfigController::dataDir() const
{
    return QDir::toNativeSeparators(app().userDataRoot());
}

bool ConfigController::dataDirOverridden() const
{
    return !app().settings().userDataDirOverride().isEmpty();
}

QString ConfigController::dataForgeBase() const
{
    // The folder holding <channel>\cache\dataforge (which may not exist yet).
    QString path = QDir::cleanPath(app().dataForgeDir());
    for (int i = 0; i < 3; ++i)
        path = path.left(std::max<qsizetype>(0, path.lastIndexOf(u'/')));
    return QDir::toNativeSeparators(path);
}

bool ConfigController::dataForgeOverridden() const
{
    return !app().settings().cacheDirOverride().isEmpty();
}

bool ConfigController::includeNewLines() const
{
    return app().settings().includeNewLines();
}

void ConfigController::setIncludeNewLines(bool on)
{
    app().settings().setIncludeNewLines(on);
    emit changed();
}

bool ConfigController::tutorialDisabled() const
{
    return app().settings().tutorialDisabled();
}

void ConfigController::setTutorialDisabled(bool on)
{
    app().settings().setTutorialDisabled(on);
    emit changed();
}

// ── folders ───────────────────────────────────────────────────────────────

void ConfigController::setDataDir(const QString &path)
{
    const QString target = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    if (target.isEmpty())
        return;
    const QString old = app().userDataRoot();
    if (QDir(target) == QDir(old))
        return;
    if (QFileInfo::exists(target) && !QFileInfo(target).isDir()) {
        app().prompts()->warning(text("config.invalid_data_folder_title"),
                                 text("config.invalid_data_folder_body", {{QStringLiteral("path"), target}}));
        return;
    }
    const auto apply = [this, target, old] {
        if (!QDir().mkpath(target)) {
            app().prompts()->warning(text("config.invalid_data_folder_title"),
                                     text("config.invalid_data_folder_body", {{QStringLiteral("path"), target}}));
            return;
        }
        app().saveUserIni();
        // Back at the default location: drop the override rather than pin it.
        Settings &settings = app().settings();
        settings.setUserDataDirOverride({});
        const bool isDefault = QDir(app().userDataRoot()) == QDir(target);
        settings.setUserDataDirOverride(isDefault ? QString() : target);
        qCInfo(lcApp) << "data folder changed:" << old << "->" << target;
        const bool oldHasData = QDir(old).exists() && !QDir(old).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot);
        const auto finish = [this] {
            app().notifyPathsChanged();
            emit changed();
            app().reloadFromDisk(text("dialogs.merging_sources"));
        };
        if (!oldHasData) {
            finish();
            return;
        }
        PromptService::Prompt p;
        p.kind = PromptService::Kind::Question;
        p.title = text("config.migrate_data_title");
        p.text = text("config.migrate_data_body", {{QStringLiteral("old_dir"), QDir::toNativeSeparators(old)},
                                                 {QStringLiteral("new_dir"), QDir::toNativeSeparators(target)}});
        p.buttons = {text("scx.yes"), text("scx.no")};
        app().prompts()->ask(p, [this, old, target, finish](int button, bool, int) {
            if (button == 0) {
                const int count = migrateUserDataDir(old, target, true);
                app().prompts()->info(text("config.migrate_data_done_title"),
                                      text("config.migrate_data_done_body", {{QStringLiteral("count"), count}}));
            }
            finish();
        });
    };
    if (onedrive::isOneDrivePath(target)) {
        app().prompts()->confirm(text("config.onedrive_folder_title"),
                                 text("config.onedrive_folder_body", {{QStringLiteral("path"), target}}), apply,
                                 PromptService::Kind::Warning);
        return;
    }
    apply();
}

void ConfigController::resetDataDir()
{
    if (!dataDirOverridden())
        return;
    Settings &settings = app().settings();
    const QString override = settings.userDataDirOverride();
    settings.setUserDataDirOverride({});
    const QString defaultRoot = app().userDataRoot();
    settings.setUserDataDirOverride(override);
    setDataDir(defaultRoot);
}

void ConfigController::setDataForgeBase(const QString &path)
{
    const QString target = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    if (target.isEmpty() || QDir(target) == QDir(dataForgeBase()))
        return;
    app().settings().setCacheDirOverride(target);
    app().notifyPathsChanged();
    emit changed();
    app().prompts()->confirm(text("extract.dataforge_outdated_title"), text("scx.dataforge_moved_body"),
                             [this] { app().extractDataForge(false); });
}

void ConfigController::resetDataForgeBase()
{
    if (!dataForgeOverridden())
        return;
    app().settings().setCacheDirOverride({});
    app().notifyPathsChanged();
    emit changed();
}

// ── user.ini tools ────────────────────────────────────────────────────────

void ConfigController::resetUserIni()
{
    const UserIni ini(app().paths().userIni());
    const QString channel = app().channel();
    if (!QFileInfo::exists(ini.path())) {
        app().prompts()->info(text("dialogs.nothing_to_reset_title"),
                              text("dialogs.nothing_to_reset_body", {{QStringLiteral("channel"), channel},
                                                                   {QStringLiteral("path"), QDir::toNativeSeparators(ini.path())}}));
        return;
    }
    const QString body =
        QStringLiteral("This will remove every custom string override for the %1 channel.\n\nFile: %2\nSize: %3 KB\n\n"
                       "A timestamped backup will be saved next to the original (user.ini.bak-YYYYMMDD-HHMMSS) so you "
                       "can restore by renaming it back to user.ini.\n\nThis does NOT touch the game's global.ini: to "
                       "revert what the game shows, apply again after the reset, or use Restore Backup.\n\nProceed?")
            .arg(channel, QDir::toNativeSeparators(ini.path()), QString::number(QFileInfo(ini.path()).size() / 1024.0, 'f', 1));
    app().prompts()->confirm(
        text("dialogs.reset_user_ini_title"), body,
        [this, channel] {
            const UserIni file(app().paths().userIni());
            const QString backup = file.reset(true);
            if (QFileInfo::exists(file.path())) {
                app().prompts()->error(text("dialogs.reset_failed_title"),
                                       text("dialogs.reset_failed_body", {{QStringLiteral("error"), QStringLiteral("could not rename %1").arg(file.path())}}));
                return;
            }
            app().reloadFromDisk(QStringLiteral("Reloading %1 after user.ini reset...").arg(channel));
            const QString note =
                backup.isEmpty() ? QString() : text("user_ini_reset.backup_note", {{QStringLiteral("path"), QDir::toNativeSeparators(backup)}});
            app().setStatus(text("status_bar.user_ini_reset", {{QStringLiteral("channel"), channel}}));
            app().prompts()->info(text("user_ini_reset.complete_title"),
                                  text("user_ini_reset.complete_body", {{QStringLiteral("channel"), channel},
                                                                      {QStringLiteral("backup_note"), note}}));
        },
        PromptService::Kind::Warning);
}

void ConfigController::restoreUserIni()
{
    const UserIni ini(app().paths().userIni());
    const QString channel = app().channel();
    const QFileInfoList snapshots = ini.backups();
    if (snapshots.isEmpty()) {
        app().prompts()->info(text("restore_user_ini.no_snapshots_title"),
                              text("restore_user_ini.no_snapshots_body", {{QStringLiteral("channel"), channel}}));
        return;
    }
    QStringList labels;
    for (const QFileInfo &fi : snapshots) {
        QFile f(fi.absoluteFilePath());
        const int lines = f.open(QIODevice::ReadOnly) ? static_cast<int>(f.readAll().count('\n')) : 0;
        labels << text("restore_user_ini.snapshot_label",
                     {{QStringLiteral("when"), fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))},
                      {QStringLiteral("lines"), lines},
                      {QStringLiteral("size_kb"), QString::number(fi.size() / 1024.0, 'f', 1)}});
    }
    PromptService::Prompt p;
    p.kind = PromptService::Kind::Question;
    p.title = text("restore_user_ini.picker_title");
    p.text = text("restore_user_ini.picker_label", {{QStringLiteral("channel"), channel}});
    p.choices = labels;
    p.buttons = {text("scx.restore"), text("scx.cancel")};
    app().prompts()->ask(p, [this, snapshots, channel](int button, bool, int choice) {
        if (button != 0 || choice < 0 || choice >= snapshots.size())
            return;
        const QFileInfo chosen = snapshots[choice];
        const UserIni file(app().paths().userIni());
        if (!file.restore(chosen.absoluteFilePath())) {
            app().prompts()->error(text("restore_user_ini.restore_failed_title"),
                                   text("restore_user_ini.restore_failed_body", {{QStringLiteral("error_type"), QStringLiteral("OSError")},
                                                                               {QStringLiteral("error"), chosen.absoluteFilePath()}}));
            return;
        }
        app().reloadFromDisk(text("progress.reloading_after_user_ini_restore", {{QStringLiteral("channel"), channel}}));
        app().setStatus(text("status_bar.user_ini_restored", {{QStringLiteral("channel"), channel}}));
        app().prompts()->info(text("restore_user_ini.restored_title"),
                              text("restore_user_ini.restored_body", {{QStringLiteral("channel"), channel},
                                                                    {QStringLiteral("name"), chosen.fileName()}}));
    });
}

void ConfigController::previewApply()
{
    const auto &entries = app().strings()->entries();
    if (entries.isEmpty()) {
        app().prompts()->warning(text("dialogs.warning_title"), text("config.no_sources_warning"));
        return;
    }
    QMap<QString, int> sourceCounts;
    QHash<QString, int> enhancementCats;
    QHash<QString, int> statusCounts;
    for (const StringEntry &e : entries) {
        const QString contributing = e.customValue.isEmpty() ? e.sourceFile : kSourceUser;
        ++sourceCounts[contributing];
        if (contributing == kSourceEnhancements)
            ++enhancementCats[e.category];
        ++statusCounts[statusName(e.status)];
    }
    const auto byCount = [](const QHash<QString, int> &h) {
        QList<std::pair<QString, int>> v;
        for (auto it = h.cbegin(); it != h.cend(); ++it)
            v.push_back({it.key(), it.value()});
        std::stable_sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
        return v;
    };
    QString body = text("config.preview_header");
    int shown = 0;
    for (const QString &name : {kSourceGlobal, kSourceEnhancements, kSourceUser}) {
        const int count = sourceCounts.value(name);
        if (count == 0)
            continue;
        ++shown;
        if (name == kSourceEnhancements) {
            body += QStringLiteral("  %1. %2 Enhancements (%3 keys total):\n").arg(shown).arg(app().appName(), thousands(count));
            for (const auto &[cat, n] : byCount(enhancementCats))
                body += QStringLiteral("       %1: %2\n").arg(cat, thousands(n));
        } else {
            body += QStringLiteral("  %1. %2 (%3 keys)\n").arg(shown).arg(name.left(1).toUpper() + name.mid(1), thousands(count));
        }
    }
    body += text("config.preview_total", {{QStringLiteral("count"), static_cast<int>(entries.size())}});
    for (const auto &[status, n] : byCount(statusCounts))
        body += QStringLiteral("  %1: %2\n").arg(status, thousands(n));
    app().prompts()->info(text("config.preview_title"), body);
}

// ── Import INI ────────────────────────────────────────────────────────────

void ConfigController::importIni(const QString &sourceText)
{
    QString source = sourceText.trimmed();
    if (source.isEmpty())
        return;
    if (source.startsWith(u"file:"))
        source = QUrl(source).toLocalFile();
    if (source.startsWith(u"http://") || source.startsWith(u"https://")) {
        if (source.startsWith(u"https://github.com/")) {
            source.replace(QStringLiteral("https://github.com/"), QStringLiteral("https://raw.githubusercontent.com/"));
            source.replace(QStringLiteral("/blob/"), QStringLiteral("/"));
        }
        auto temp = std::make_shared<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/scx-import-XXXXXX.ini"));
        temp->setAutoRemove(false);
        if (!temp->open())
            return;
        const QString tempPath = temp->fileName();
        temp->close();
        QFile::remove(tempPath);
        app().setStatus(text("status_bar.downloading_ini"));
        app().tasks()->run<QString>(
            text("status_bar.downloading_ini"), true,
            [source, tempPath](TaskRunner::Job &job) -> QString {
                net::DownloadOptions options;
                options.cancel = job.cancelFlag();
                const auto r = net::downloadIfChanged(QUrl(source), tempPath, options);
                return r.ok() ? QString() : r.error;
            },
            [this, source, tempPath](QString error) {
                if (!error.isEmpty()) {
                    app().prompts()->error(text("import_flow.download_error_title"),
                                           text("import_flow.download_error_body", {{QStringLiteral("source"), source},
                                                                                  {QStringLiteral("error"), error}}));
                    QFile::remove(tempPath);
                    QFile::remove(tempPath + QStringLiteral(".etag"));
                    return;
                }
                importFromFile(tempPath, tempPath);
            });
        return;
    }
    if (!QFileInfo::exists(source)) {
        app().prompts()->warning(text("dialogs.file_not_found_title"),
                                 text("dialogs.file_not_found_body", {{QStringLiteral("path"), source}}));
        return;
    }
    importFromFile(source, {});
}

void ConfigController::importFromFile(const QString &file, const QString &tempToRemove)
{
    const IniMap imported = loadIni(file);
    if (!tempToRemove.isEmpty()) {
        QFile::remove(tempToRemove);
        QFile::remove(tempToRemove + QStringLiteral(".etag"));
    }
    if (imported.isEmpty()) {
        app().prompts()->warning(text("dialogs.empty_file_title"), text("dialogs.empty_file_body"));
        return;
    }
    const IniMap &defaults = app().strings()->defaults();
    if (defaults.isEmpty()) {
        app().prompts()->warning(text("import_flow.no_base_data_title"), text("import_flow.no_base_data_body"));
        return;
    }
    int valid = 0;
    excluded_ = 0;
    pendingAdd_.clear();
    current_.clear();
    conflicts_.clear();
    const UserIni ini(app().paths().userIni());
    const IniMap currentUser = ini.load();
    for (const auto &[k, v] : currentUser)
        current_.insert(k, v);
    for (const auto &[key, value] : imported) {
        if (!defaults.contains(key)) {
            ++excluded_;
            continue;
        }
        ++valid;
        const QString *current = currentUser.find(key);
        if (!current)
            pendingAdd_.insert(key, value);
        else if (*current != value)
            conflicts_ << QVariantMap{{QStringLiteral("key"), key},
                                      {QStringLiteral("current"), *current},
                                      {QStringLiteral("imported"), value}};
    }
    if (valid == 0) {
        app().prompts()->warning(text("import_flow.no_valid_keys_title"),
                                 text("import_flow.no_valid_keys_body", {{QStringLiteral("count"), static_cast<int>(imported.size())}}));
        return;
    }
    if (pendingAdd_.isEmpty() && conflicts_.isEmpty()) {
        app().prompts()->info(text("import_flow.nothing_to_import_title"), text("import_flow.nothing_to_import_body"));
        return;
    }
    emit importConflictsChanged();
    if (conflicts_.isEmpty()) {
        app().prompts()->confirm(text("import_flow.confirm_title"),
                                 text("import_flow.confirm_body", {{QStringLiteral("new_count"), static_cast<int>(pendingAdd_.size())},
                                                                 {QStringLiteral("excluded_count"), excluded_}}),
                                 [this] { writeImport({}, 0); });
        return;
    }
    emit importReady();
}

void ConfigController::finishImport(const QVariantList &resolutions)
{
    QMap<QString, QString> resolved;
    for (qsizetype i = 0; i < conflicts_.size() && i < resolutions.size(); ++i) {
        const QVariantMap c = conflicts_[i].toMap();
        const QString current = c.value(QStringLiteral("current")).toString();
        const QString imported = c.value(QStringLiteral("imported")).toString();
        const QString how = resolutions[i].toString();
        QString value = current;
        if (how == u"use")
            value = imported;
        else if (how == u"append")
            value = current + imported;
        else if (how == u"prepend")
            value = imported + current;
        else if (how.startsWith(u"custom:"))
            value = how.sliced(7);
        resolved.insert(c.value(QStringLiteral("key")).toString(), value);
    }
    writeImport(resolved, static_cast<int>(resolved.size()));
}

void ConfigController::cancelImport()
{
    conflicts_.clear();
    pendingAdd_.clear();
    current_.clear();
    emit importConflictsChanged();
}

void ConfigController::writeImport(const QMap<QString, QString> &resolved, int resolvedCount)
{
    // dict(current); update(auto_add); update(resolutions): current order first.
    app().saveUserIni(); // table edits not yet saved go in first
    const UserIni ini(app().paths().userIni());
    IniMap final = ini.load();
    for (auto it = pendingAdd_.cbegin(); it != pendingAdd_.cend(); ++it)
        final.insert(it.key(), it.value());
    for (auto it = resolved.cbegin(); it != resolved.cend(); ++it)
        final.insert(it.key(), it.value());
    if (!ini.save(final)) {
        app().prompts()->error(text("import_flow.error_title"),
                               text("import_flow.error_body", {{QStringLiteral("error"), QStringLiteral("could not write %1").arg(ini.path())}}));
        return;
    }
    const int added = static_cast<int>(pendingAdd_.size());
    const int excluded = excluded_;
    cancelImport();
    app().reloadFromDisk(text("import_flow.reload_status"));
    app().prompts()->info(text("import_flow.complete_title"),
                          text("import_flow.complete_body", {{QStringLiteral("added"), added},
                                                           {QStringLiteral("resolved"), resolvedCount},
                                                           {QStringLiteral("excluded"), excluded}}));
}

// ── settings backups ──────────────────────────────────────────────────────

QString ConfigController::defaultSettingsBackupPath() const
{
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(docs).filePath(profile::defaultBackupFilename());
}

void ConfigController::exportSettings(const QUrl &target)
{
    const QString path = target.isLocalFile() ? target.toLocalFile() : target.toString();
    app().saveUserIni();
    const QVariantMap values = profile::exportSettingsValues(app().settings());
    QMap<QString, QString> overrides;
    for (const QString &channel : app().channels()) {
        QFile f(QDir(app().userDataRoot()).filePath(channel + QStringLiteral("/user.ini")));
        if (f.open(QIODevice::ReadOnly))
            overrides.insert(channel, QString::fromUtf8(f.readAll()));
    }
    const auto written = profile::writeProfileZip(path, values, overrides, app().version(),
                                                  profile::kSourceInstalled);
    if (!written) {
        app().prompts()->error(text("settings_backup.export_failed_title"),
                               text("settings_backup.export_failed_body", {{QStringLiteral("error_type"), QStringLiteral("OSError")},
                                                                         {QStringLiteral("error"), written.error()}}));
        return;
    }
    app().setStatus(text("status_bar.settings_exported"));
    QStringList channels = overrides.keys();
    app().prompts()->info(text("settings_backup.export_done_title"),
                          text("settings_backup.export_done_body",
                             {{QStringLiteral("path"), QDir::toNativeSeparators(path)},
                              {QStringLiteral("n_settings"), static_cast<int>(values.size())},
                              {QStringLiteral("channels"), channels.isEmpty() ? text("settings_backup.no_channels") : channels.join(QStringLiteral(", "))}}));
}

void ConfigController::importSettings(const QUrl &source)
{
    const QString path = source.isLocalFile() ? source.toLocalFile() : source.toString();
    const auto profile = profile::readProfileZip(path);
    if (!profile) {
        app().prompts()->error(text("settings_backup.import_invalid_title"),
                               text("settings_backup.import_invalid_body", {{QStringLiteral("error"), profile.error()}}));
        return;
    }
    const QStringList channelList = profile->overrides.keys();
    const QString channels = channelList.isEmpty() ? text("settings_backup.no_channels") : channelList.join(QStringLiteral(", "));
    const QString when = profile->exportedAt.isEmpty() ? QStringLiteral("?") : QString(profile->exportedAt).replace(u'T', u' ');
    const auto contents = *profile;
    app().prompts()->confirm(
        text("settings_backup.import_confirm_title"),
        text("settings_backup.import_confirm_body", {{QStringLiteral("version"), profile->appVersion.isEmpty() ? QStringLiteral("?") : profile->appVersion},
                                                   {QStringLiteral("when"), when},
                                                   {QStringLiteral("n_settings"), static_cast<int>(profile->settings.size())},
                                                   {QStringLiteral("channels"), channels}}),
        [this, contents, channels] {
            const QDir root(app().userDataRoot());
            for (auto it = contents.overrides.cbegin(); it != contents.overrides.cend(); ++it) {
                const UserIni ini(root.filePath(it.key() + QStringLiteral("/user.ini")));
                QDir().mkpath(QFileInfo(ini.path()).absolutePath());
                if (QFileInfo::exists(ini.path()))
                    ini.backup();
                QFile f(ini.path());
                if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    app().prompts()->error(text("settings_backup.import_failed_title"),
                                           text("settings_backup.import_failed_body", {{QStringLiteral("error_type"), QStringLiteral("OSError")},
                                                                                     {QStringLiteral("error"), ini.path()}}));
                    return;
                }
                f.write(it.value().toUtf8());
            }
            const int applied = profile::importSettingsValues(app().settings(), contents.settings);
            const auto outcome = profile::reconcileImportedInstallPath(app().settings());
            const QString rootPath = app().installRoot();
            QString body;
            if (outcome == profile::InstallPathOutcome::Restored)
                body = text("settings_backup.import_done_body_restored", {{QStringLiteral("applied"), applied}, {QStringLiteral("channels"), channels}, {QStringLiteral("root"), rootPath}});
            else if (outcome == profile::InstallPathOutcome::Redetected)
                body = text("settings_backup.import_done_body_detected", {{QStringLiteral("applied"), applied}, {QStringLiteral("channels"), channels}, {QStringLiteral("root"), rootPath}});
            else
                body = text("settings_backup.import_done_body_no_install", {{QStringLiteral("applied"), applied}, {QStringLiteral("channels"), channels}});
            app().settings().sync();
            app().notifyPathsChanged();
            emit changed();
            app().prompts()->info(text("settings_backup.import_done_title"), body);
            app().reloadFromDisk(text("dialogs.merging_sources"));
        });
}

// ── language sources ──────────────────────────────────────────────────────

QVariantList ConfigController::languageSources() const
{
    QFile f(QDir(app().languagesDir()).filePath(QStringLiteral("sources.json")));
    QJsonObject bundled;
    if (f.open(QIODevice::ReadOnly))
        bundled = QJsonDocument::fromJson(f.readAll()).object();
    QVariantList out;
    for (const QVariant &v : app().languages()) {
        const QVariantMap lang = v.toMap();
        const QString id = lang.value(QStringLiteral("id")).toString();
        if (id == kDefaultLanguage)
            continue;
        out << QVariantMap{{QStringLiteral("id"), id},
                           {QStringLiteral("name"), lang.value(QStringLiteral("name"))},
                           {QStringLiteral("url"), app().settings().languageSourceOverride(id)},
                           {QStringLiteral("bundled"), bundled.value(id).toString()}};
    }
    return out;
}

void ConfigController::setLanguageSource(const QString &language, const QString &url)
{
    app().settings().setLanguageSourceOverride(language, url.trimmed());
}
