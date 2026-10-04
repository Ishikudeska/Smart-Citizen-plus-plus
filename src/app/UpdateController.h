#pragma once

#include "core/net/AppUpdater.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

class AppController;

// The in-app update check against the configured GitHub repository, and
// the download and silent run of a newer release's installer. Owned by
// App; QML reaches it as App.updates.
class UpdateController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by App")

    Q_PROPERTY(bool enabled READ enabled CONSTANT)

public:
    explicit UpdateController(QObject *parent = nullptr);

    // False when this build has no update repository configured.
    bool enabled() const;
    // `interactive` reports "up to date" and failures too; the startup check
    // only speaks up when a newer release exists.
    Q_INVOKABLE void check(bool interactive);

private:
    AppController &app() const;
    void onCheck(const core::net::UpdateCheck &check, const QString &current, bool interactive);
    void downloadAndInstall(const core::net::UpdateCheck &check);
};
