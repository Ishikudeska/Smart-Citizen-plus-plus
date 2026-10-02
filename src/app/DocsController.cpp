#include "DocsController.h"

#include <QCoreApplication>
#include <QFile>

DocsController::DocsController(QObject *parent) : QObject(parent) {}

QString DocsController::markdown(const QString &name, const QString &language) const
{
    QStringList candidates;
    // About and Legal describe this app and are English only; FAQ and Help have translations.
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
        return text;
    }
    return {};
}
