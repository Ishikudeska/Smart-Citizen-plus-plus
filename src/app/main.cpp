#include "core/AppInfo.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QTimer>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    core::applyIdentity();

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.loadFromModule("ScApp", "Main");

    // Used by CTest and CI: load the UI, then exit cleanly.
    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"))) {
        if (engine.rootObjects().isEmpty())
            return EXIT_FAILURE;
        QTimer::singleShot(500, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
