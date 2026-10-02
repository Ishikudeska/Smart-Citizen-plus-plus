#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

// The About, FAQ, Legal and Help pages: bundled Markdown in the selected
// language (FAQ and Help have translations), English otherwise.
class DocsController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit DocsController(QObject *parent = nullptr);

    // name: "ABOUT", "FAQ", "HELP" or "LEGAL".
    Q_INVOKABLE QString markdown(const QString &name, const QString &language) const;
};
