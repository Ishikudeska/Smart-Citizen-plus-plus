#include "AppController.h"

#include "core/AppInfo.h"
#include "core/ScInstall.h"
#include "core/apply/ApplyService.h"
#include "core/apply/Backups.h"
#include "core/apply/LocPack.h"
#include "core/blueprints/OwnedItems.h"
#include "core/enhancements/Generator.h"
#include "core/merge/SourceLoader.h"
#include "core/model/Enhancements.h"
#include "core/net/Downloader.h"
#include "core/pipeline/Extraction.h"
#include "core/text/IniFile.h"
#include "core/user/UserIni.h"
#include "core/util/OneDrive.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QTimer>
#include <QStandardPaths>

#include <filesystem>
#include <map>

Q_LOGGING_CATEGORY(lcApp, "scx.app")

using namespace core;

namespace {

AppController *g_instance = nullptr;

std::filesystem::path fs(const QString &path)
{
    return std::filesystem::path(path.toStdU16String());
}

QString dataForgeStampPath(const QString &enhancementsDir)
{
    return QDir(enhancementsDir).filePath(QStringLiteral(".dataforge_stamp"));
}

QString readSmallFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}

struct LoadResult
{
    QList<StringEntry> entries;
    IniMap defaults;
    QStringList problems;
    QMap<QString, blueprints::BlueprintItem> blueprintMeta;
    QSet<QString> knownItemNames;
};

} // namespace

AppController::AppController(MainInstance, QObject *parent)
    : QObject(parent), settings_(std::make_unique<Settings>()), paths_(std::make_unique<Paths>(*settings_)),
      strings_(new StringTableModel(this)), tasks_(new TaskRunner(this)), prompts_(new PromptService(this))
{
    g_instance = this;
    updates_ = new UpdateController(this);
    windowLayout_ = new WindowLayout(this);
    translator_ = new i18n::JsonTranslator(this);
    installTranslator(language());
    QCoreApplication::installTranslator(translator_);
    strings_->retranslate();

    strings_->setFavoritePrefix(favoritePrefix());
    strings_->setAppStamp(appName(), version());
    strings_->setBlueprintHeader(settings_->missionHeader(QStringLiteral("blueprints")));
    connect(strings_, &StringTableModel::edited, this, [this] {
        setApplyDirty(true);
        if (initialLoadDone_)
            unappliedEdit_ = true;
    });
    connect(tasks_, &TaskRunner::runningChanged, this, &AppController::busyChanged);
    connect(tasks_, &TaskRunner::runningChanged, this, &AppController::onTasksRunningChanged);
    connect(tasks_, &TaskRunner::failed, this,
            [this](const QString &title, const QString &message) { prompts_->error(title, message); });
}

AppController::~AppController()
{
    if (g_instance == this)
        g_instance = nullptr;
}

AppController *AppController::instance()
{
    return g_instance;
}

AppController *AppController::create(QQmlEngine *engine, QJSEngine *)
{
    Q_ASSERT(g_instance);
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    g_instance->setEngine(engine);
    return g_instance;
}

// ── simple properties ─────────────────────────────────────────────────────

QString AppController::appName() const
{
    return QCoreApplication::applicationName();
}

QString AppController::version() const
{
    return QCoreApplication::applicationVersion();
}

QString AppController::buildInfo() const
{
    return buildDescription();
}

QString AppController::channel() const
{
    return settings_->activeChannel();
}

QStringList AppController::channels() const
{
    return core::channels();
}

QStringList AppController::installedChannels() const
{
    return installRoot().isEmpty() ? QStringList() : core::installedChannels(installRoot());
}

QString AppController::installRoot() const
{
    return settings_->scInstallRoot();
}

QString AppController::language() const
{
    return settings_->selectedLanguage();
}

QVariantList AppController::languages() const
{
    QVariantList out;
    for (const auto langs = i18n::availableLanguages(languagesDir()); const QString &lang : langs) {
        QString name = lang;
        name.replace(u'_', u' ');
        QStringList words = name.split(u' ');
        for (QString &w : words)
            if (!w.isEmpty())
                w = w.left(1).toUpper() + w.mid(1).toLower();
        out << QVariantMap{{QStringLiteral("id"), lang}, {QStringLiteral("name"), words.join(u' ')}};
    }
    return out;
}

QString AppController::theme() const
{
    return settings_->theme();
}

void AppController::setTheme(const QString &theme)
{
    if (theme == this->theme())
        return;
    settings_->setTheme(theme);
    emit themeChanged();
}

QString AppController::uiMode() const
{
    return settings_->uiMode();
}

void AppController::setUiMode(const QString &mode)
{
    if (mode == uiMode())
        return;
    settings_->setUiMode(mode);
    emit uiModeChanged();
}

QString AppController::favoritePrefix() const
{
    return settings_->favoritePrefix();
}

void AppController::setFavoritePrefix(const QString &prefix)
{
    const QString old = favoritePrefix();
    if (prefix == old || prefix.isEmpty())
        return;
    // Re-prefix favourites in memory and on disk (#140).
    for (StringEntry &e : strings_->mutableEntries())
        if (e.customValue.startsWith(old))
            e.customValue = prefix + e.customValue.sliced(old.size());
    settings_->setFavoritePrefix(prefix);
    strings_->setFavoritePrefix(prefix);
    saveUserIni();
    emit favoritePrefixChanged();
    reload();
}

QString AppController::languagesDir() const
{
    return QStringLiteral(":/languages");
}

QString AppController::baseIniPath() const
{
    return paths_->baseIni();
}

QString AppController::gameGlobalIni() const
{
    return paths_->gameGlobalIni();
}

QString AppController::userDataRoot() const
{
    return paths_->userDataRoot();
}

QString AppController::dataForgeDir() const
{
    return paths_->dataForgeCacheDir();
}

QString AppController::p4kPath() const
{
    return paths_->p4kPath();
}

QString AppController::p4kStatus() const
{
    const QString p4k = p4kPath();
    if (p4k.isEmpty() || !QFileInfo::exists(p4k))
        return QStringLiteral("noarchive");
    const QString base = paths_->baseIni(kDefaultLanguage);
    if (!QFileInfo::exists(base))
        return QStringLiteral("missing");
    return baseIniIsFresh(p4k, base) ? QStringLiteral("fresh") : QStringLiteral("stale");
}

QString AppController::dataForgeStatus() const
{
    const QString p4k = p4kPath();
    if (p4k.isEmpty() || !QFileInfo::exists(p4k))
        return QStringLiteral("noarchive");
    if (!QFileInfo(dataForgeRecordsDir(dataForgeDir())).isDir())
        return QStringLiteral("missing");
    return dataForgeCacheIsFresh(p4k, dataForgeDir()) ? QStringLiteral("fresh") : QStringLiteral("stale");
}

QString AppController::channelInstallDir() const
{
    return paths_->channelInstallDir();
}

QString AppController::text(const char *key, const QVariantHash &args) const
{
    return i18n::tr(key, args);
}

QString AppController::fmt(const QString &text, const QVariantMap &args) const
{
    QVariantHash hash;
    for (auto it = args.begin(); it != args.end(); ++it)
        hash.insert(it.key(), it.value());
    return i18n::format(text, hash);
}

QString AppController::tr(const QString &key, const QVariantMap &args) const
{
    return fmt(QCoreApplication::translate("", key.toUtf8().constData()), args);
}

void AppController::setStatus(const QString &text)
{
    if (statusText_ == text)
        return;
    statusText_ = text;
    emit statusTextChanged();
}

void AppController::setApplyDirty(bool dirty)
{
    if (applyDirty_ == dirty)
        return;
    applyDirty_ = dirty;
    emit applyDirtyChanged();
}

void AppController::copyText(const QString &text)
{
    if (QClipboard *cb = QGuiApplication::clipboard())
        cb->setText(text);
}

void AppController::copyFilteredRows()
{
    const QString tsv = strings_->filteredTsv();
    if (tsv.isEmpty()) {
        prompts_->info(text("dialogs.copy_filtered_title"), text("dialogs.copy_filtered_empty"));
        return;
    }
    copyText(tsv);
    prompts_->info(text("dialogs.copy_filtered_title"),
                   text("dialogs.copy_filtered_done", {{QStringLiteral("count"), strings_->visibleCount()}}));
}

QString AppController::urlToPath(const QUrl &url) const
{
    return url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile()) : url.toString();
}

QUrl AppController::pathToUrl(const QString &path) const
{
    return QUrl::fromLocalFile(path);
}

// ── channel, install and language ────────────────────────────────────────

void AppController::setInstallRoot(const QString &root)
{
    const QString normalized = normalizeInstallRoot(root);
    if (normalized.isEmpty()) {
        prompts_->warning(text("dialogs.warning_title"), text("scx.not_an_install", {{QStringLiteral("path"), root}}));
        return;
    }
    if (normalized == installRoot())
        return;
    settings_->setScInstallRoot(normalized);
    const QStringList installed = core::installedChannels(normalized);
    if (!installed.isEmpty() && !installed.contains(channel()))
        settings_->setActiveChannel(installed.front());
    emit installChanged();
    emit channelChanged();
    emit pathsChanged();
    checkP4kThenLoad();
}

void AppController::detectInstall()
{
    tasks_->run<QString>(text("scx.detecting_install"), false, [](TaskRunner::Job &) { return locateScInstall(); },
                         [this](const QString &found) {
                             if (found.isEmpty()) {
                                 prompts_->info(text("extract.path_required_title"), text("scx.no_install_found"));
                                 return;
                             }
                             setInstallRoot(found);
                         });
}

void AppController::setChannel(const QString &channel)
{
    if (channel == this->channel() || !core::channels().contains(channel))
        return;
    saveUserIni(); // the old channel's edits belong to the old user.ini
    settings_->setActiveChannel(channel);
    enhancementsPrompted_ = false;
    emit channelChanged();
    emit pathsChanged();
    setStatus(text("status_bar.channel_switched_reloading", {{QStringLiteral("channel"), channel}}));
    checkP4kThenLoad();
}

void AppController::installTranslator(const QString &language)
{
    translator_->setCatalog(std::make_shared<i18n::Catalog>(i18n::Catalog::load(languagesDir(), language)));
}

void AppController::setLanguage(const QString &language)
{
    if (language == this->language())
        return;
    saveUserIni();
    settings_->setSelectedLanguage(language);
    installTranslator(language);
    if (engine_)
        engine_->retranslate();
    strings_->retranslate();
    emit languageChanged();
    emit pathsChanged();
    setStatus(text("dialogs.language_changed_status", {{QStringLiteral("language"), language}}));

    if (language == kDefaultLanguage) {
        loadEntries(text("dialogs.merging_sources"));
        return;
    }
    const QString dest = paths_->baseIni(language);
    const QString url = languageBaseUrl(*settings_, language, languagesDir());
    const auto afterBase = [this, language, dest] {
        if (!QFileInfo::exists(dest)) {
            prompts_->warning(text("dialogs.app_title"),
                              text("dialogs.language_download_failed", {{QStringLiteral("language"), language}}));
            loadEntries(text("dialogs.merging_sources"));
            return;
        }
        if (enhancementsFresh() || !QFileInfo(dataForgeRecordsDir(dataForgeDir())).isDir()) {
            loadEntries(text("dialogs.merging_sources"));
            return;
        }
        setStatus(text("dialogs.language_generating_enhancements", {{QStringLiteral("language"), language}}));
        generateEnhancements();
    };
    if (url.isEmpty()) {
        if (!QFileInfo::exists(dest))
            setStatus(text("dialogs.language_no_url", {{QStringLiteral("language"), language}}));
        afterBase();
        return;
    }
    tasks_->run<bool>(
        text("dialogs.language_downloading", {{QStringLiteral("language"), language}}), true,
        [url, dest](TaskRunner::Job &job) {
            net::DownloadOptions options;
            options.cancel = job.cancelFlag();
            const auto result = net::downloadIfChanged(QUrl(url), dest, options);
            if (!result.ok())
                qCWarning(lcApp) << "language download failed:" << result.error;
            return result.ok();
        },
        [afterBase](bool) { afterBase(); });
}

// ── startup ───────────────────────────────────────────────────────────────

void AppController::startup()
{
    if (startupDone_)
        return;
    startupDone_ = true;
    qCInfo(lcApp).noquote() << buildInfo();
    if (updates_->enabled())
        updates_->check(false);
    warnIfOneDrive();
    if (installRoot().isEmpty()) {
        const QString found = locateScInstall();
        if (!found.isEmpty()) {
            settings_->setScInstallRoot(found);
            const QStringList installed = core::installedChannels(found);
            if (!installed.isEmpty() && !installed.contains(channel()))
                settings_->setActiveChannel(installed.front());
            emit installChanged();
            emit channelChanged();
            emit pathsChanged();
            qCInfo(lcApp) << "found Star Citizen at" << found;
        }
    }
    if (!QFileInfo::exists(paths_->baseIni(kDefaultLanguage)) && installRoot().isEmpty()) {
        prompts_->info(text("extract.path_required_title"), text("extract.path_required_body"));
        emit navigateTo(QStringLiteral("config"));
        setStatus(text("status_bar.no_strings_loaded"));
        return;
    }
    checkP4kThenLoad();
}

void AppController::warnIfOneDrive()
{
    if (settings_->oneDriveWarningDismissed())
        return;
    const QString dataDir = userDataRoot();
    if (!onedrive::isOneDrivePath(dataDir))
        return;
    const QString local = onedrive::suggestLocalDataDir(appName());
    PromptService::Prompt p;
    p.kind = PromptService::Kind::Warning;
    p.title = text("onedrive.warning_title");
    p.text = text("onedrive.warning_body", {{QStringLiteral("data_dir"), dataDir}, {QStringLiteral("local"), local}});
    p.buttons = {text("onedrive.move_btn"), text("onedrive.keep_here_btn")};
    p.checkbox = text("onedrive.dont_warn_again");
    prompts_->ask(p, [this, dataDir, local](int button, bool dontWarn, int) {
        if (dontWarn)
            settings_->setOneDriveWarningDismissed(true);
        if (button == 0) {
            const int moved = migrateUserDataDir(dataDir, local, false);
            settings_->setUserDataDirOverride(local);
            emit pathsChanged();
            qCInfo(lcApp) << "moved" << moved << "files to" << local;
            reload();
        }
    });
}

void AppController::checkP4kThenLoad()
{
    const QString status = p4kStatus();
    if (status == u"missing" || status == u"stale") {
        PromptService::Prompt p;
        p.kind = PromptService::Kind::Question;
        p.title = text("extract.p4k_prompt_title");
        p.text = text(status == u"missing" ? "extract.p4k_prompt_base_missing" : "extract.p4k_prompt_p4k_newer");
        p.buttons = {text("scx.yes"), text("scx.no")};
        prompts_->ask(p, [this](int button, bool, int) {
            if (button == 0) {
                extractFromP4k(false);
            } else if (QFileInfo::exists(baseIniPath())) {
                loadEntries(text("progress.reading_sources"), [this] { checkEnhancementsFreshness(); });
            } else {
                setStatus(text("status_bar.no_strings_loaded"));
            }
        });
        return;
    }
    if (!QFileInfo::exists(paths_->baseIni(kDefaultLanguage)) && !QFileInfo::exists(baseIniPath())) {
        setStatus(text("status_bar.no_strings_loaded"));
        return;
    }
    if (dataForgeStatus() == u"stale" && QFileInfo::exists(QDir(dataForgeDir()).filePath(kP4kStampName))) {
        prompts_->confirm(text("extract.dataforge_outdated_title"), text("extract.dataforge_outdated_body"),
                          [this] { extractDataForge(true); });
    }
    loadEntries(text("progress.reading_sources"), [this] { checkEnhancementsFreshness(); });
}

void AppController::checkEnhancementsFreshness()
{
    if (!QFileInfo::exists(baseIniPath()) || tasks_->running())
        return;
    const QString dir = paths_->enhancementsDir();
    QStringList missing;
    for (const auto ids = settings_->enabledEnhancementFileIds(); const QString &id : ids)
        if (!QFileInfo::exists(QDir(dir).filePath(enhancements::fileNameFor(id))))
            missing << id;
    if (missing.isEmpty() || p4kStatus() == u"noarchive")
        return;
    if (enhancementsPrompted_)
        return;
    enhancementsPrompted_ = true;
    PromptService::Prompt p;
    p.kind = PromptService::Kind::Question;
    p.title = text("extract.generate_dialog_title");
    p.text = text("scx.generate_missing_body", {{QStringLiteral("count"), missing.size()}});
    p.detail = missing.join(u'\n');
    p.buttons = {text("scx.generate_now"), text("scx.later")};
    prompts_->ask(p, [this](int button, bool, int) {
        if (button == 0)
            generateEnhancements();
    });
}

// ── loading ───────────────────────────────────────────────────────────────

void AppController::reload()
{
    loadEntries(text("dialogs.merging_sources"));
}

void AppController::reloadFromDisk(const QString &message)
{
    for (StringEntry &e : strings_->mutableEntries())
        e.customValue.clear();
    loadEntries(message);
}

void AppController::loadEntries(const QString &message, std::function<void()> then)
{
    SourceFiles files;
    files.baseIni = QFileInfo::exists(baseIniPath()) ? baseIniPath() : paths_->baseIni(kDefaultLanguage);
    files.enhancementsDir = paths_->enhancementsDir();
    files.enhancementFileIds = settings_->enabledEnhancementFileIds();
    files.userIni = paths_->userIni();
    const blueprints::Enclosings enclosing = enclosings();
    const QString bpHeader = blueprintHeader();
    tasks_->run<LoadResult>(
        message, false,
        [files, enclosing, bpHeader](TaskRunner::Job &job) {
            job.report(i18n::tr("progress.reading_sources"));
            LoadedSources loaded = loadSources(files);
            job.report(i18n::tr("progress.creating_entries"));
            LoadResult r;
            r.entries = buildEntries(loaded);
            if (const auto it = loaded.sources.find(kSourceGlobal); it != loaded.sources.end())
                r.defaults = it->second;
            r.problems = loaded.problems;
            QHash<QString, QString> stock;
            stock.reserve(r.defaults.size());
            for (const auto &[k, v] : r.defaults)
                stock.insert(k, v);
            r.blueprintMeta = blueprints::buildBlueprintMetadata(r.entries, enclosing, stock, bpHeader);
            r.knownItemNames = blueprints::knownItemNames(r.entries, enclosing, stock);
            return r;
        },
        [this, then = std::move(then)](LoadResult r) {
            const QHash<QString, QString> pending = table::pendingEdits(strings_->entries());
            const int restored = table::restorePendingEdits(r.entries, pending);
            if (restored)
                qCInfo(lcApp) << "restored" << restored << "in-memory edits not yet in user.ini";
            for (const QString &p : std::as_const(r.problems))
                qCWarning(lcApp).noquote() << p;
            qCInfo(lcApp) << "loaded" << r.entries.size() << "strings";
            blueprintMeta_ = std::move(r.blueprintMeta);
            knownItemNames_ = std::move(r.knownItemNames);
            if (!knownItemNames_.isEmpty()) {
                // Names another localization editor left in the owned set (#372).
                const auto repair = blueprints::repairForeignOwnedNames(settings_->ownedItems(), knownItemNames_);
                if (!repair.renamed.isEmpty()) {
                    for (auto it = repair.renamed.cbegin(); it != repair.renamed.cend(); ++it)
                        qCInfo(lcApp) << "owned set: repaired" << it.key() << "->" << it.value().value_or(QStringLiteral("(duplicate)"));
                    settings_->setOwnedItems(repair.repaired);
                }
            }
            strings_->setEntries(std::move(r.entries), std::move(r.defaults));
            recomputeOwned();
            loaded_ = strings_->totalCount() > 0;
            emit loadedChanged();
            afterLoad();
            if (then)
                then();
        });
}

void AppController::afterLoad()
{
    setApplyDirty(true);
    if (initialLoadDone_)
        unappliedEdit_ = true;
    initialLoadDone_ = true;
    if (strings_->totalCount() == 0)
        setStatus(text("status_bar.no_strings_loaded"));
    else
        setStatus(text("status_bar.entry_count", {{QStringLiteral("count"), strings_->totalCount()}}));
    emit entriesReloaded();
}

blueprints::Enclosings AppController::enclosings()
{
    return blueprints::enclosingsFromTagConfigs(settings_->allTagConfigs());
}

QString AppController::blueprintHeader() const
{
    return settings_->missionHeader(QStringLiteral("blueprints"));
}

void AppController::recomputeOwned()
{
    const QSet<QString> owned = settings_->ownedItems();
    const blueprints::Enclosings enc = enclosings();
    const QString header = blueprintHeader();
    for (StringEntry &e : strings_->mutableEntries()) {
        const QString woven = blueprints::applyOwnedToValue(e.originalValue, owned, enc, header);
        if (woven != e.originalValue)
            e.originalValue = woven;
    }
    table::OwnedState state;
    for (auto it = blueprintMeta_.cbegin(); it != blueprintMeta_.cend(); ++it)
        state.blueprintItems.insert(it.key());
    state.owned = owned;
    state.enclosings = enc;
    strings_->setOwnedState(std::move(state));
    setApplyDirty(true);
    emit blueprintsChanged();
}

bool AppController::saveUserIni()
{
    if (strings_->totalCount() == 0)
        return true;
    const UserIni ini(paths_->userIni());
    const auto saved = ini.save(strings_->entries());
    if (!saved)
        qCWarning(lcApp) << "could not save" << ini.path();
    return saved.has_value();
}

// ── applying ──────────────────────────────────────────────────────────────

void AppController::applyToGame()
{
    if (strings_->totalCount() == 0) {
        prompts_->warning(text("dialogs.warning_title"), text("dialogs.no_file_loaded"));
        return;
    }
    if (installRoot().isEmpty() || channelInstallDir().isEmpty()) {
        prompts_->warning(text("dialogs.warning_title"), text("dialogs.no_game_path"));
        emit navigateTo(QStringLiteral("config"));
        return;
    }
    const UserIni ini(paths_->userIni());
    const auto userCount = ini.save(strings_->entries());
    if (!userCount) {
        prompts_->error(text("apply.cannot_save_edits_title"),
                        text("apply.cannot_save_edits_body", {{QStringLiteral("path"), ini.path()},
                                                              {QStringLiteral("error_type"), QStringLiteral("OSError")},
                                                              {QStringLiteral("error"), QStringLiteral("write failed")}}));
        return;
    }

    ApplyInputs in;
    SourceFiles files;
    files.baseIni = QFileInfo::exists(baseIniPath()) ? baseIniPath() : paths_->baseIni(kDefaultLanguage);
    files.enhancementsDir = paths_->enhancementsDir();
    files.enhancementFileIds = settings_->enabledEnhancementFileIds();
    files.userIni = paths_->userIni();
    in.userOverrides = userOverridesFrom(strings_->entries());
    in.baseIniPath = files.baseIni;
    in.gameFile = gameGlobalIni();
    in.backupsDir = paths_->backupsDir();
    in.channelInstallDir = channelInstallDir();
    in.scLanguageId = scLanguageId(language());
    in.appName = appName();
    in.version = version();
    if (!settings_->includeNewLines())
        for (const StringEntry &e : strings_->entries())
            if (e.status == EntryStatus::New && e.customValue.isEmpty())
                in.excludeEnhancementKeys.insert(e.key);
    in.languagesIniSource = QDir(languagesDir()).filePath(language() + QStringLiteral("/languages.ini"));
    in.languagesIniDest = QDir(channelInstallDir()).filePath(QStringLiteral("data/languages.ini"));
    if (const QSet<QString> owned = settings_->ownedItems(); !owned.isEmpty())
        in.beforeStamps = [owned, enc = enclosings(), header = blueprintHeader()](IniMap &merged) {
            IniMap woven;
            woven.reserve(merged.size());
            for (const auto &[k, v] : merged)
                woven.insert(k, blueprints::applyOwnedToValue(v, owned, enc, header));
            merged = std::move(woven);
        };

    QHash<QString, int> enhancementCounts;
    int enhancementTotal = 0;
    for (const StringEntry &e : strings_->entries())
        if (e.sourceFile == kSourceEnhancements) {
            ++enhancementCounts[e.category];
            ++enhancementTotal;
        }
    const int users = *userCount;

    tasks_->run<ApplyOutcome>(
        text("scx.applying"), false,
        [in = std::move(in), files](TaskRunner::Job &) mutable {
            in.sources = loadSources(files);
            return core::applyToGame(in);
        },
        [this, users, enhancementCounts, enhancementTotal](ApplyOutcome out) {
            if (!out.validation.isEmpty()) {
                const QString note = out.restoredBackup
                                         ? QStringLiteral("\n\nThe previous file has been restored from backup:\n%1")
                                               .arg(QFileInfo(out.backupPath).fileName())
                                         : QStringLiteral("\n\nNo backup was available to restore.");
                setStatus(text("dialogs.apply_failed_status"));
                prompts_->error(text("dialogs.validation_failed_title"),
                                text("dialogs.validation_failed_body", {{QStringLiteral("msg"), out.validation},
                                                                        {QStringLiteral("restore_note"), note}}));
                return;
            }
            if (!out.ok) {
                prompts_->error(text("dialogs.error_title"), text("apply.failed_body", {{QStringLiteral("error"), out.error}}));
                return;
            }
            // Biggest category first, as Counter.most_common orders them.
            QList<std::pair<QString, int>> cats;
            for (auto it = enhancementCounts.cbegin(); it != enhancementCounts.cend(); ++it)
                cats.push_back({it.key(), it.value()});
            std::stable_sort(cats.begin(), cats.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
            QString block;
            if (cats.isEmpty()) {
                block = QStringLiteral("  %1 enhancements: 0").arg(appName());
            } else {
                QStringList lines;
                for (const auto &[cat, count] : cats)
                    lines << QStringLiteral("    %1: %2").arg(cat, QLocale(QLocale::English).toString(count));
                block = QStringLiteral("  %1 enhancements (%2 total):\n%3")
                            .arg(appName(), QLocale(QLocale::English).toString(enhancementTotal), lines.join(u'\n'));
            }
            setStatus(text("dialogs.apply_status", {{QStringLiteral("user_count"), users},
                                                    {QStringLiteral("enhancement_count"), enhancementTotal}}));
            prompts_->info(text("dialogs.success_title"),
                           text("apply.applied_body", {{QStringLiteral("target_path"), gameGlobalIni()},
                                                       {QStringLiteral("user_count"), QLocale(QLocale::English).toString(users)},
                                                       {QStringLiteral("enhancement_block"), block}}));
            setApplyDirty(false);
            unappliedEdit_ = false;
        });
}

QVariantList AppController::backups() const
{
    QVariantList out;
    for (const auto backups = GameFileBackups(paths_->backupsDir()).list(); const QFileInfo &fi : backups)
        out << QVariantMap{{QStringLiteral("path"), fi.absoluteFilePath()},
                           {QStringLiteral("name"), fi.fileName()},
                           {QStringLiteral("time"), fi.lastModified()}};
    return out;
}

void AppController::restoreBackup(const QString &path)
{
    if (installRoot().isEmpty()) {
        prompts_->warning(text("dialogs.warning_title"), text("dialogs.no_game_path"));
        return;
    }
    const QString error = GameFileBackups::restore(path, gameGlobalIni());
    if (!error.isEmpty()) {
        prompts_->error(text("dialogs.error_title"), text("restore_backup.error_body", {{QStringLiteral("error"), error}}));
        return;
    }
    qCInfo(lcApp) << "restored backup" << path << "to" << gameGlobalIni();
    reload();
    prompts_->info(text("dialogs.success_title"),
                   text("restore_backup.success_body", {{QStringLiteral("name"), QFileInfo(path).fileName()}}));
}

void AppController::clearLocalization()
{
    if (installRoot().isEmpty()) {
        prompts_->warning(text("dialogs.warning_title"), text("dialogs.no_game_path"));
        return;
    }
    const QString file = gameGlobalIni();
    if (!QFileInfo::exists(file)) {
        prompts_->info(text("dialogs.nothing_to_clear_title"), text("dialogs.nothing_to_clear_body"));
        return;
    }
    prompts_->confirm(text("dialogs.clear_localization_title"),
                      text("dialogs.clear_localization_body",
                           {{QStringLiteral("loc_dir"), QDir::toNativeSeparators(QFileInfo(file).absolutePath())}}),
                      [this, file] {
                          if (!QFile::remove(file)) {
                              prompts_->error(text("dialogs.error_title"),
                                              text("dialogs.failed_to_delete_global_ini",
                                                   {{QStringLiteral("error"), QStringLiteral("could not delete %1").arg(file)}}));
                              return;
                          }
                          qCInfo(lcApp) << "deleted" << file;
                          setStatus(text("dialogs.clear_localization_status"));
                          prompts_->info(text("dialogs.clear_localization_done_title"),
                                         text("dialogs.clear_localization_done_body"));
                          setApplyDirty(true);
                      });
}

void AppController::clearCache()
{
    const QDir cache(paths_->cacheDir());
    const QFileInfoList files = cache.entryInfoList({QStringLiteral("*.ini"), QStringLiteral("*.txt")}, QDir::Files, QDir::Name);
    const bool hasForge = QFileInfo(dataForgeDir()).isDir();
    if (files.isEmpty() && !hasForge) {
        prompts_->info(text("dialogs.cache_empty_title"), text("dialogs.cache_empty_body"));
        return;
    }
    QStringList names;
    for (const QFileInfo &f : files)
        names << QStringLiteral("  ") + f.fileName();
    const QString body = QStringLiteral("This will delete the following cached files:\n\n%1\n\nbase.ini will need to be "
                                        "re-extracted from Data.p4k before strings can be loaded.")
                             .arg(names.join(u'\n'));
    prompts_->confirm(text("dialogs.clear_cache_title"), body, [this, files, hasForge] {
        QStringList deleted, failed;
        for (const QFileInfo &f : files) {
            if (QFile::remove(f.absoluteFilePath()))
                deleted << f.fileName();
            else
                failed << f.fileName();
        }
        const auto finish = [this](const QStringList &gone, const QStringList &stuck) {
            strings_->setEntries({}, {});
            loaded_ = false;
            emit loadedChanged();
            emit pathsChanged();
            QString msg = QStringLiteral("Deleted %1 item(s) from cache.").arg(gone.size());
            if (!stuck.isEmpty())
                msg += QStringLiteral("\n\nFailed to delete:\n") + stuck.join(u'\n');
            prompts_->info(text("dialogs.cache_cleared_title"), msg);
            checkP4kThenLoad();
        };
        if (!hasForge) {
            finish(deleted, failed);
            return;
        }
        prompts_->confirm(
            text("dialogs.dataforge_cache_title"),
            QStringLiteral("Also clear the DataForge entity cache?\n\nRecreating it takes a little while.\n\n"
                           "The DataForge cache holds the extracted entity data used to generate ship and weapon "
                           "stats. Keep it if you only want to refresh the localization strings.\n\nClear DataForge cache?"),
            [this, deleted, failed, finish]() mutable {
                if (QDir(dataForgeDir()).removeRecursively())
                    deleted << QStringLiteral("dataforge/");
                else
                    failed << QStringLiteral("dataforge/");
                finish(deleted, failed);
            },
            PromptService::Kind::Question, [deleted, failed, finish] { finish(deleted, failed); });
    });
}

void AppController::openLocalizationDir()
{
    if (installRoot().isEmpty()) {
        prompts_->warning(text("dialogs.warning_title"), text("dialogs.no_game_path"));
        return;
    }
    const QString dir = QFileInfo(gameGlobalIni()).absolutePath();
    if (!QFileInfo(dir).isDir()) {
        prompts_->warning(text("dialogs.dir_not_found_title"),
                          QStringLiteral("Localization directory not found:\n%1\n\nCheck your game install path on the "
                                         "Config page.")
                              .arg(QDir::toNativeSeparators(dir)));
        return;
    }
    openFolder(dir);
}

void AppController::openFolder(const QString &path)
{
    QDir().mkpath(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString AppController::defaultLocPackPath() const
{
    QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (downloads.isEmpty() || !QFileInfo(downloads).isDir())
        downloads = QDir::homePath();
    return QDir(downloads).filePath(defaultLocPackFilename(channel()));
}

void AppController::exportLocPack(const QUrl &target)
{
    if (installRoot().isEmpty()) {
        prompts_->warning(text("dialogs.warning_title"), text("dialogs.no_game_path"));
        return;
    }
    const QString source = gameGlobalIni();
    if (!QFileInfo::exists(source)) {
        prompts_->info(text("dialogs.nothing_to_export_title"), text("dialogs.nothing_to_export_body"));
        return;
    }
    const QString out = target.isLocalFile() ? target.toLocalFile() : target.toString();
    const auto size = writeLocPackZip(source, out);
    if (!size) {
        prompts_->error(text("dialogs.export_failed_title"), text("dialogs.export_failed_body", {{QStringLiteral("error"), size.error()}}));
        return;
    }
    prompts_->info(text("dialogs.export_complete_title"),
                   text("dialogs.export_complete_body", {{QStringLiteral("out_path"), QDir::toNativeSeparators(out)},
                                                         {QStringLiteral("channel"), channel()},
                                                         {QStringLiteral("source_size"), *size},
                                                         {QStringLiteral("zip_size"), QFileInfo(out).size()}}));
}

// ── extraction and generation ─────────────────────────────────────────────

void AppController::extractFromP4k(bool thenGenerate)
{
    const QString p4k = p4kPath();
    if (p4k.isEmpty() || !QFileInfo::exists(p4k)) {
        prompts_->warning(text("extract.path_required_title"), text("extract.path_required_body"));
        emit navigateTo(QStringLiteral("config"));
        return;
    }
    const QString base = paths_->baseIni(kDefaultLanguage);
    tasks_->run<QString>(
        text("extract.p4k_extraction_title"), false,
        [p4k, base](TaskRunner::Job &job) -> QString {
            job.report(i18n::tr("progress.copy_global_short"));
            auto archive = engine::p4k::Archive::open(fs(p4k));
            if (!archive)
                return QString::fromStdString(archive.error().message);
            if (auto r = extractBaseIni(**archive, base); !r)
                return QString::fromStdString(r.error().message);
            return {};
        },
        [this, thenGenerate](const QString &error) {
            emit pathsChanged();
            if (!error.isEmpty()) {
                simpleRunActive_ = false;
                prompts_->warning(text("extract.extraction_error_title"), error);
                return;
            }
            loadEntries(text("scx.reloading_extracted"), [this, thenGenerate] {
                if (thenGenerate)
                    extractDataForge(true);
                else
                    checkEnhancementsFreshness();
            });
        });
}

void AppController::extractDataForge(bool thenGenerate)
{
    const QString p4k = p4kPath();
    if (p4k.isEmpty() || !QFileInfo::exists(p4k)) {
        prompts_->warning(text("extract.path_required_title"), text("extract.path_required_body"));
        return;
    }
    const QString cache = dataForgeDir();
    const QString patches = QStringLiteral(":/patches");
    tasks_->run<QString>(
        text("extract.dataforge_extraction_title"), true,
        [p4k, cache, patches](TaskRunner::Job &job) -> QString {
            auto archive = engine::p4k::Archive::open(fs(p4k));
            if (!archive)
                return QString::fromStdString(archive.error().message);
            auto result = core::extractDataForge(
                **archive, cache, patches,
                [&job](const QString &step, qint64 done, qint64 total) {
                    job.report(step, static_cast<int>(done), static_cast<int>(total));
                },
                job.cancelFlag());
            if (!result)
                return QString::fromStdString(result.error().message);
            qCInfo(lcApp).noquote() << "DataForge cache:" << result->records << "records;"
                                    << result->patches.summary();
            return {};
        },
        [this, thenGenerate](const QString &error) {
            emit pathsChanged();
            if (!error.isEmpty()) {
                simpleRunActive_ = false;
                prompts_->warning(text("extract.dataforge_extraction_error_title"),
                                  text("extract.dataforge_extraction_error_body", {{QStringLiteral("message"), error}}));
                return;
            }
            setStatus(text("extract.dataforge_extracted_generating"));
            if (thenGenerate)
                generateEnhancements();
        });
}

bool AppController::enhancementsFresh() const
{
    const QStringList enabled = settings_->enabledEnhancementFileIds();
    if (enabled.isEmpty())
        return true;
    const QString dir = paths_->enhancementsDir();
    const QString stamp = readSmallFile(dataForgeStampPath(dir));
    const QString build = readSmallFile(QDir(dataForgeDir()).filePath(kP4kStampName));
    if (stamp.isEmpty() || stamp != build)
        return false;
    for (const QString &id : enabled)
        if (!QFileInfo::exists(QDir(dir).filePath(enhancements::fileNameFor(id))))
            return false;
    return true;
}

void AppController::writeEnhancementsStamp()
{
    const QString build = readSmallFile(QDir(dataForgeDir()).filePath(kP4kStampName));
    QFile f(dataForgeStampPath(paths_->enhancementsDir()));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(build.toUtf8());
}

void AppController::generateEnhancements()
{
    if (!QFileInfo(dataForgeRecordsDir(dataForgeDir())).isDir() || dataForgeStatus() == u"stale") {
        if (p4kStatus() == u"noarchive") {
            simpleRunActive_ = false;
            prompts_->warning(text("extract.path_required_title"), text("extract.path_required_body"));
            return;
        }
        extractDataForge(true);
        return;
    }
    enh::GeneratorOptions options = enh::optionsFromSettings(*settings_);
    options.baseIni = baseIniPath();
    if (!QFileInfo::exists(options.baseIni))
        options.baseIni = paths_->baseIni(kDefaultLanguage);
    options.englishBaseIni = paths_->baseIni(kDefaultLanguage);
    options.forgeDir = dataForgeDir();
    options.patchesDir = QStringLiteral(":/patches");
    // The generator writes beside its base.ini; a language's INIs go to its folder.
    tasks_->run<QString>(
        text("progress.generating_enhancements_title"), true,
        [options](TaskRunner::Job &job) -> QString {
            job.report(i18n::tr("progress.generating_enhancements"));
            const auto result = enh::generateEnhancements(options, &job.progress(), &job.cancelToken());
            if (!result)
                return result.error();
            qCInfo(lcApp) << "generated" << result->entries << "enhancement entries";
            return {};
        },
        [this](const QString &error) {
            if (!error.isEmpty()) {
                simpleRunActive_ = false;
                setStatus(text("status_bar.enhancement_generation_failed"));
                prompts_->error(text("dialogs.error_title"), error);
                return;
            }
            writeEnhancementsStamp();
            if (simpleRunActive_) {
                simpleRunActive_ = false;
                setStatus(text("status_bar.enhancements_generated_applying"));
                loadEntries(text("progress.reloading_with_enhancements"), [this] { applyToGame(); });
                return;
            }
            setStatus(text("status_bar.enhancements_generated_reloading"));
            loadEntries(text("progress.reloading_with_enhancements"));
        });
}

void AppController::simpleApply()
{
    if (busy())
        return;
    // Applying needs the game folder, and Config (where it is set) is hidden
    // in Simple mode, so send the user to Advanced rather than do nothing.
    if (installRoot().isEmpty() || channelInstallDir().isEmpty()) {
        prompts_->info(text("simple_mode.set_game_folder_title"), text("simple_mode.set_game_folder_body"));
        setUiMode(QStringLiteral("advanced"));
        emit navigateTo(QStringLiteral("config"));
        return;
    }
    prompts_->confirm(text("simple_mode.apply_enhancements_confirm_title"),
                      text("simple_mode.apply_enhancements_confirm_body"), [this] {
                          if (busy())
                              return;
                          simpleRunActive_ = true;
                          // Without a current base.ini there is nothing to apply over:
                          // extract it first (which continues into DataForge and generation).
                          const QString base = p4kStatus();
                          if ((base == u"missing" || base == u"stale" || strings_->totalCount() == 0) && base != u"noarchive")
                              extractFromP4k(true);
                          else
                              generateEnhancements();
                      });
}

// ── closing ───────────────────────────────────────────────────────────────

bool AppController::requestClose()
{
    // Quitting mid-job would hide the window but keep the process alive
    // until the job ended (Qt waits for the thread pool on exit). Cancel it
    // instead and close once idle; a job that cannot be cancelled (applying)
    // finishes its writes first.
    if (tasks_->running()) {
        if (!closeWhenIdle_) {
            closeWhenIdle_ = true;
            tasks_->cancelAll();
        }
        return false;
    }
    if (unappliedEdit_) {
        PromptService::Prompt p;
        p.kind = PromptService::Kind::Warning;
        p.title = text("dialogs.unapplied_changes_title");
        p.text = text("dialogs.unapplied_changes_body");
        p.buttons = {text("dialogs.unapplied_changes_apply_now"), text("dialogs.unapplied_changes_exit"),
                     text("scx.cancel")};
        prompts_->ask(p, [this](int button, bool, int) {
            if (button == 0) {
                applyToGame();
            } else if (button == 1) {
                unappliedEdit_ = false;
                quit();
            }
        });
        return false;
    }
    quit();
    return true;
}

void AppController::onTasksRunningChanged()
{
    if (!closeWhenIdle_)
        return;
    if (tasks_->running()) {
        tasks_->cancel(); // a job chained by the cancelled one's `done`
        return;
    }
    // Deferred so the finished job's `done`, which runs after this signal,
    // can chain another job first.
    QTimer::singleShot(0, this, [this] {
        if (!closeWhenIdle_ || tasks_->running())
            return;
        closeWhenIdle_ = false;
        requestClose();
    });
}

void AppController::quit()
{
    if (strings_->totalCount() > 0) {
        const UserIni ini(paths_->userIni());
        if (ini.shouldAutosave(strings_->entries()))
            ini.save(strings_->entries());
    }
    settings_->sync();
    QCoreApplication::quit();
}
