#include "UpdateController.h"

#include "AppController.h"
#include "PromptService.h"
#include "TaskRunner.h"

#include "core/AppIdentity.h"
#include "core/i18n/Translator.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QLoggingCategory>
#include <QPointer>
#include <QProcess>
#include <QThreadPool>

Q_DECLARE_LOGGING_CATEGORY(lcApp)

using namespace core;

namespace {

QString text(const char *key, const QVariantHash &args = {})
{
    return i18n::tr(key, args);
}

} // namespace

UpdateController::UpdateController(QObject *parent) : QObject(parent) {}

AppController &UpdateController::app() const
{
    return *AppController::instance();
}

bool UpdateController::enabled() const
{
    return !QString::fromLatin1(identity::kUpdateRepo).isEmpty();
}

void UpdateController::check(bool interactive)
{
    const QString repo = QString::fromLatin1(identity::kUpdateRepo);
    if (repo.isEmpty()) {
        if (interactive)
            app().prompts()->info(text("config.check_updates_btn"), text("scx.update_check_disabled"));
        return;
    }
    if (interactive)
        app().setStatus(text("status_bar.update_checking"));
    QPointer<UpdateController> self(this);
    const QString current = app().version();
    QThreadPool::globalInstance()->start([self, repo, current, interactive] {
        net::UpdateCheck check = net::checkForUpdate(repo, current);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, check = std::move(check), current, interactive] {
                if (self)
                    self->onCheck(check, current, interactive);
            },
            Qt::QueuedConnection);
    });
}

void UpdateController::onCheck(const net::UpdateCheck &check, const QString &current, bool interactive)
{
    using Status = net::UpdateCheck::Status;
    if (check.status == Status::Failed) {
        qCWarning(lcApp).noquote() << "App update check failed:" << check.error;
        if (interactive) {
            app().setStatus(text("status_bar.update_check_failed"));
            app().prompts()->warning(text("dialogs.update_check_failed_title"),
                                     text("dialogs.update_check_failed_body", {{QStringLiteral("message"), check.error}}));
        }
        return;
    }
    if (check.status != Status::Available) {
        qCInfo(lcApp).noquote() << "App is up to date at" << current;
        if (interactive) {
            app().setStatus(text("status_bar.update_up_to_date", {{QStringLiteral("version"), current}}));
            app().prompts()->info(text("dialogs.up_to_date_title"),
                                  text("dialogs.up_to_date_body", {{QStringLiteral("current"), current}}));
        }
        return;
    }
    qCInfo(lcApp).noquote() << "App update available:" << check.latest << "(current" << current
                            << (check.installer ? "with installer)" : "without installer)");
    app().setStatus(text("status_bar.update_available", {{QStringLiteral("version"), check.latest}}));
    // Installers are for installed builds; a portable build updates by hand.
    const bool canInstall = check.installer.has_value() && !identity::kPortable;
    PromptService::Prompt p;
    p.kind = PromptService::Kind::Info;
    p.title = text("dialogs.update_available_title");
    p.text = text("dialogs.update_available_body",
                  {{QStringLiteral("latest"), check.latest}, {QStringLiteral("current"), current}});
    if (canInstall)
        p.text += QStringLiteral("\n\n") + text("dialogs.update_auto_note");
    p.detail = check.notes;
    if (canInstall)
        p.buttons << text("dialogs.update_now");
    p.buttons << text("dialogs.update_open_release") << text("dialogs.update_later");
    app().prompts()->ask(p, [this, check, canInstall](int button, bool, int) {
        const int open = canInstall ? 1 : 0;
        if (canInstall && button == 0)
            downloadAndInstall(check);
        else if (button == open)
            QDesktopServices::openUrl(check.releasePage);
    });
}

void UpdateController::downloadAndInstall(const net::UpdateCheck &check)
{
    const net::InstallerAsset asset = *check.installer;
    const QString dest = QDir(QDir::tempPath()).filePath(app().appName().remove(u' ') + QStringLiteral("-Update"));
    struct Downloaded
    {
        QString path, error;
        bool cancelled = false;
    };
    app().tasks()->run<Downloaded>(
        text("dialogs.update_download_title"), true,
        [asset, dest, latest = check.latest](TaskRunner::Job &job) -> Downloaded {
            const QString label = i18n::tr("dialogs.update_download_label", {{QStringLiteral("latest"), latest}});
            job.report(label);
            Downloaded d;
            d.path = net::downloadInstaller(
                asset, dest, &d.error,
                [&job, &label](qint64 done, qint64 total) {
                    job.report(label, static_cast<int>(done >> 10), static_cast<int>(total >> 10));
                },
                job.cancelFlag());
            d.cancelled = job.cancelled();
            return d;
        },
        [this](Downloaded d) {
            if (d.cancelled)
                return;
            if (d.path.isEmpty()) {
                app().prompts()->warning(text("dialogs.update_download_failed_title"),
                                         text("dialogs.update_download_failed_body", {{QStringLiteral("message"), d.error}}));
                return;
            }
            // /AUTOUPDATE=1 makes the installer relaunch the app when it is done.
            const QStringList args{QStringLiteral("/SILENT"), QStringLiteral("/NORESTART"),
                                   QStringLiteral("/SUPPRESSMSGBOXES"), QStringLiteral("/AUTOUPDATE=1")};
            if (!QProcess::startDetached(d.path, args)) {
                qCWarning(lcApp).noquote() << "Update installer failed to start:" << d.path;
                app().prompts()->warning(text("dialogs.update_launch_failed_title"),
                                         text("dialogs.update_launch_failed_body",
                                              {{QStringLiteral("path"), QDir::toNativeSeparators(d.path)}}));
                return;
            }
            qCInfo(lcApp).noquote() << "Update installer launched:" << d.path << "- exiting to install";
            app().quit();
        });
}
