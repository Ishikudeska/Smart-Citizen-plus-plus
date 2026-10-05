#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class AppController;

// Machine-local sizes: the window's geometry and the String Editor's column
// widths. Owned by App; QML reaches it as App.windowLayout.
class WindowLayout : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by App")

public:
    explicit WindowLayout(QObject *parent = nullptr);

    // {x, y, width, height, maximized}, empty if never saved.
    Q_INVOKABLE QVariantMap windowGeometry() const;
    Q_INVOKABLE void saveWindowGeometry(const QVariantMap &geometry);
    // -1 = the default for that column; empty if never resized.
    Q_INVOKABLE QVariantList columnWidths() const;
    Q_INVOKABLE void saveColumnWidths(const QVariantList &widths);
    // Asks, then forgets both and signals windowProportionsReset().
    Q_INVOKABLE void resetWindowProportions();

signals:
    void windowProportionsReset();

private:
    AppController &app() const;
};
