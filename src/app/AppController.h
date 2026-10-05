#pragma once

#include "PromptService.h"
#include "StringTableModel.h"
#include "TaskRunner.h"
#include "UpdateController.h"
#include "WindowLayout.h"
#include "core/Paths.h"
#include "core/Settings.h"
#include "core/blueprints/BlueprintMeta.h"
#include "core/i18n/Translator.h"

#include <QObject>
#include <QQmlEngine>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

// The app's state and every top-level action, exposed to QML as the `App`
// singleton. Ports the orchestration in main_window.py: startup checks,
// loading the strings table, applying to the game, backups, channel and
// language switches, extraction and enhancement generation. Pages call in
// here; nothing in QML touches files.
class AppController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString buildInfo READ buildInfo CONSTANT)
    Q_PROPERTY(StringTableModel *strings READ strings CONSTANT)
    Q_PROPERTY(TaskRunner *tasks READ tasks CONSTANT)
    Q_PROPERTY(PromptService *prompts READ prompts CONSTANT)
    Q_PROPERTY(UpdateController *updates READ updates CONSTANT)
    Q_PROPERTY(WindowLayout *windowLayout READ windowLayout CONSTANT)
    Q_PROPERTY(QString channel READ channel WRITE setChannel NOTIFY channelChanged)
    Q_PROPERTY(QStringList channels READ channels CONSTANT)
    Q_PROPERTY(QStringList installedChannels READ installedChannels NOTIFY installChanged)
    Q_PROPERTY(QString installRoot READ installRoot WRITE setInstallRoot NOTIFY installChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QString uiMode READ uiMode WRITE setUiMode NOTIFY uiModeChanged)
    Q_PROPERTY(
        QString favoritePrefix READ favoritePrefix WRITE setFavoritePrefix NOTIFY favoritePrefixChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool applyDirty READ applyDirty NOTIFY applyDirtyChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString languagesDir READ languagesDir CONSTANT)
    Q_PROPERTY(QString baseIniPath READ baseIniPath NOTIFY pathsChanged)
    Q_PROPERTY(QString gameGlobalIni READ gameGlobalIni NOTIFY pathsChanged)
    Q_PROPERTY(QString userDataRoot READ userDataRoot NOTIFY pathsChanged)
    Q_PROPERTY(QString dataForgeDir READ dataForgeDir NOTIFY pathsChanged)
    Q_PROPERTY(QString p4kPath READ p4kPath NOTIFY pathsChanged)
    Q_PROPERTY(QString p4kStatus READ p4kStatus NOTIFY pathsChanged)
    Q_PROPERTY(QString dataForgeStatus READ dataForgeStatus NOTIFY pathsChanged)

public:
    // Not default-constructible, so QML uses create() and gets main()'s
    // instance instead of making its own.
    struct MainInstance
    {
    };
    explicit AppController(MainInstance, QObject *parent = nullptr);
    ~AppController() override;

    // The instance main() made; QML gets this one.
    static AppController *instance();
    static AppController *create(QQmlEngine *, QJSEngine *);
    void setEngine(QQmlEngine *engine) { engine_ = engine; }

    core::Settings &settings() { return *settings_; }
    const core::Paths &paths() const { return *paths_; }
    core::i18n::JsonTranslator *translator() const { return translator_; }

    QString appName() const;
    QString version() const;
    QString buildInfo() const;
    StringTableModel *strings() const { return strings_; }
    TaskRunner *tasks() const { return tasks_; }
    PromptService *prompts() const { return prompts_; }
    UpdateController *updates() const { return updates_; }
    WindowLayout *windowLayout() const { return windowLayout_; }
    QString channel() const;
    void setChannel(const QString &channel);
    QStringList channels() const;
    QStringList installedChannels() const;
    QString installRoot() const;
    void setInstallRoot(const QString &root);
    QString language() const;
    void setLanguage(const QString &language);
    QVariantList languages() const;
    QString theme() const;
    void setTheme(const QString &theme);
    QString uiMode() const;
    void setUiMode(const QString &mode);
    QString favoritePrefix() const;
    void setFavoritePrefix(const QString &prefix);
    QString statusText() const { return statusText_; }
    bool applyDirty() const { return applyDirty_; }
    bool loaded() const { return loaded_; }
    bool busy() const { return tasks_->running(); }
    QString languagesDir() const;
    QString baseIniPath() const;
    QString gameGlobalIni() const;
    QString userDataRoot() const;
    QString dataForgeDir() const;
    QString p4kPath() const;
    QString p4kStatus() const;       // "fresh", "stale", "missing" or "noarchive"
    QString dataForgeStatus() const; // same values

    // Startup: install detection, OneDrive warning, base.ini freshness,
    // then load; called once the window is up.
    Q_INVOKABLE void startup();
    Q_INVOKABLE void reload();
    // Reload after user.ini was replaced on disk: in-memory edits are
    // dropped instead of carried over.
    void reloadFromDisk(const QString &message);
    void notifyPathsChanged() { emit pathsChanged(); }

    // Blueprint Tracker data, rebuilt on every load.
    const QMap<QString, core::blueprints::BlueprintItem> &blueprintMeta() const { return blueprintMeta_; }
    const QSet<QString> &knownItemNames() const { return knownItemNames_; }
    core::blueprints::Enclosings enclosings();
    QString blueprintHeader() const;
    // Weaves [Owned] into the loaded strings for the current owned set and
    // refreshes the table's Owned column.
    void recomputeOwned();
    Q_INVOKABLE void applyToGame();
    Q_INVOKABLE QVariantList backups() const; // [{path, name, time}], newest first
    Q_INVOKABLE void restoreBackup(const QString &path);
    Q_INVOKABLE void clearLocalization();
    Q_INVOKABLE void clearCache();
    Q_INVOKABLE void openLocalizationDir();
    Q_INVOKABLE void openFolder(const QString &path);
    Q_INVOKABLE QString defaultLocPackPath() const;
    Q_INVOKABLE void exportLocPack(const QUrl &target);
    Q_INVOKABLE void extractFromP4k(bool thenGenerate = false);
    Q_INVOKABLE void extractDataForge(bool thenGenerate = true);
    Q_INVOKABLE void generateEnhancements();
    // Simple mode's one button: refresh what is stale (base.ini, DataForge),
    // generate enhancements, reload, then apply to the game.
    Q_INVOKABLE void simpleApply();

    Q_INVOKABLE void detectInstall();
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void copyFilteredRows();
    Q_INVOKABLE QString fmt(const QString &text, const QVariantMap &args) const;
    Q_INVOKABLE QString tr(const QString &key, const QVariantMap &args = {}) const;
    Q_INVOKABLE void setStatus(const QString &text);
    Q_INVOKABLE bool saveUserIni();
    // Window close: false when a prompt about unapplied edits is showing;
    // QML calls quit() after the user decides. While a job runs, it is
    // cancelled (an apply finishes first) and the close resumes once idle.
    Q_INVOKABLE bool requestClose();
    Q_INVOKABLE void quit();
    Q_INVOKABLE QString urlToPath(const QUrl &url) const;
    Q_INVOKABLE QUrl pathToUrl(const QString &path) const;

signals:
    void channelChanged();
    void installChanged();
    void languageChanged();
    void themeChanged();
    void uiModeChanged();
    void favoritePrefixChanged();
    void statusTextChanged();
    void applyDirtyChanged();
    void loadedChanged();
    void busyChanged();
    void pathsChanged();
    void entriesReloaded();
    void navigateTo(const QString &page); // ask the shell to show a page ("config", ...)
    void blueprintsChanged();

private:
    void onTasksRunningChanged();
    void setApplyDirty(bool dirty);
    void installTranslator(const QString &language);
    void loadEntries(const QString &message, std::function<void()> then = {});
    void afterLoad();
    void checkP4kThenLoad();
    void checkEnhancementsFreshness();
    void warnIfOneDrive();
    void writeEnhancementsStamp();
    bool enhancementsFresh() const;
    QString channelInstallDir() const;
    QString text(const char *key, const QVariantHash &args = {}) const;

    std::unique_ptr<core::Settings> settings_;
    std::unique_ptr<core::Paths> paths_;
    core::i18n::JsonTranslator *translator_ = nullptr;
    QQmlEngine *engine_ = nullptr;
    StringTableModel *strings_ = nullptr;
    TaskRunner *tasks_ = nullptr;
    PromptService *prompts_ = nullptr;
    UpdateController *updates_ = nullptr;
    WindowLayout *windowLayout_ = nullptr;
    QString statusText_;
    bool applyDirty_ = true;
    bool loaded_ = false;
    bool initialLoadDone_ = false;
    bool unappliedEdit_ = false;
    bool closeWhenIdle_ = false;   // the window was closed while a job ran
    bool simpleRunActive_ = false; // simpleApply() continues into applyToGame()
    bool startupDone_ = false;
    bool enhancementsPrompted_ = false;
    QMap<QString, core::blueprints::BlueprintItem> blueprintMeta_;
    QSet<QString> knownItemNames_;
};
