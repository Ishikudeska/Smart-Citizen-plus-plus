#include "AppController.h"

#include "core/AppInfo.h"
#include "core/log/CrashHandler.h"
#include "core/log/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include <memory>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    core::applyIdentity();
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    const QStringList args = QCoreApplication::arguments();
    const bool smokeTest = args.contains(QStringLiteral("--smoke-test"));
    // --screenshot <file.png> [page]: render the window and exit (docs, checks).
    QString screenshot, screenshotPage;
    if (const qsizetype i = args.indexOf(QStringLiteral("--screenshot")); i >= 0 && i + 1 < args.size()) {
        screenshot = args[i + 1];
        if (i + 2 < args.size() && !args[i + 2].startsWith(u'-'))
            screenshotPage = args[i + 2];
    }

    AppController controller{AppController::MainInstance{}};
    const QString logsDir = controller.paths().logsDir();
    QDir().mkpath(logsDir);
    core::log::LogHub::instance().install(QDir(logsDir).filePath(QStringLiteral("app.log")),
                                          qEnvironmentVariableIsSet("SCX_DEBUG"));
    core::crash::install(logsDir);

    QQmlApplicationEngine engine;
    controller.setEngine(&engine);
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    // --load: read the cached strings without the startup prompts (with --screenshot).
    const bool loadOnly = args.contains(QStringLiteral("--load"));
    if (loadOnly)
        QTimer::singleShot(0, &controller, &AppController::reload);
    // --explorer <archive path>: open the P4K Explorer at that file or folder.
    QString explorerPath;
    if (const qsizetype i = args.indexOf(QStringLiteral("--explorer")); i >= 0 && i + 1 < args.size()) {
        explorerPath = args[i + 1];
        if (screenshotPage.isEmpty())
            screenshotPage = QStringLiteral("explorer");
    }
    // --tour [n]: start the guided tour, n steps in (with --screenshot).
    int tourStep = -1;
    if (const qsizetype i = args.indexOf(QStringLiteral("--tour")); i >= 0) {
        bool ok = false;
        tourStep = i + 1 < args.size() ? args[i + 1].toInt(&ok) : 0;
        if (!ok)
            tourStep = 0;
    }
    engine.setInitialProperties({{QStringLiteral("autoStart"), !smokeTest && screenshot.isEmpty() && !loadOnly},
                                 {QStringLiteral("initialPage"), screenshotPage},
                                 {QStringLiteral("explorerPath"), explorerPath},
                                 {QStringLiteral("tourStep"), tourStep}});
    engine.loadFromModule("ScApp", "Main");
    if (engine.rootObjects().isEmpty())
        return EXIT_FAILURE;

    if (smokeTest)
        QTimer::singleShot(500, &app, &QCoreApplication::quit);
    if (!screenshot.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().front());
        if (const qsizetype i = args.indexOf(QStringLiteral("--size")); i >= 0 && i + 1 < args.size()) {
            const QStringList wh = args[i + 1].split(u'x');
            if (wh.size() == 2 && window)
                window->resize(wh[0].toInt(), wh[1].toInt());
        }
        // Shoot once no job has run for a second (at most two minutes in).
        auto *poll = new QTimer(&app);
        auto idleTicks = std::make_shared<int>(0);
        auto elapsed = std::make_shared<int>(0);
        QObject::connect(poll, &QTimer::timeout, &app, [&controller, window, screenshot, poll, idleTicks, elapsed] {
            *elapsed += poll->interval();
            *idleTicks = controller.tasks()->running() || controller.tasks()->queued() ? 0 : *idleTicks + 1;
            if (*idleTicks < 4 && *elapsed < 120000)
                return;
            poll->stop();
            if (window && !window->grabWindow().save(screenshot))
                qWarning("could not save %s", qPrintable(screenshot));
            QCoreApplication::quit();
        });
        QTimer::singleShot(loadOnly ? 4000 : 1500, poll, [poll] { poll->start(250); });
    }
    return app.exec();
}
