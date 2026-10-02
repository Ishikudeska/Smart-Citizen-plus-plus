#include "DocsController.h"

#include <QCoreApplication>
#include <QFile>

DocsController::DocsController(QObject *parent) : QObject(parent) {}

QString DocsController::markdown(const QString &name, const QString &language) const
{
    QStringList candidates;
    // About and Legal describe this app; only the Smart Citizen versions are translated.
    if (name == u"FAQ" || name == u"HELP")
        candidates << QStringLiteral(":/languages/%1/%2.md").arg(language, name);
    candidates << QStringLiteral(":/docs/english/%1.md").arg(name);
    for (const QString &path : candidates) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        QString text = QString::fromUtf8(f.readAll());
        const QString app = QCoreApplication::applicationName();
        text.replace(QStringLiteral("{app}"), app);
        if (path.startsWith(u":/languages/")) // translated Smart Citizen docs
            text.replace(QStringLiteral("Smart Citizen"), app);
        return text;
    }
    return {};
}
