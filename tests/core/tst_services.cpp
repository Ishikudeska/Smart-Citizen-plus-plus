// Settings backups, loc-packs, downloads, logging, crash reports and
// progress. Ports test_settings_profile.py, test_locpack_exporter.py and
// test_progress_sink.py; tests/fixtures/smartcitizen_settings_backup.zip
// was written by Smart Citizen's own write_profile_zip.

#include "core/EnginePaths.h"
#include "core/Settings.h"
#include "core/apply/LocPack.h"
#include "core/log/CrashHandler.h"
#include "core/log/Log.h"
#include "core/net/Downloader.h"
#include "core/profile/SettingsProfile.h"
#include "core/util/Progress.h"
#include "engine/p4k/Archive.h"
#include "engine/zip/ZipWriter.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

using namespace core;

namespace {

QString s(const char *utf8)
{
    return QString::fromUtf8(utf8);
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        qFatal("cannot write %s", qPrintable(path));
    f.write(bytes);
}

// name -> content of every entry in a zip.
QMap<QString, QByteArray> zipEntries(const QString &path)
{
    QMap<QString, QByteArray> out;
    auto archive = engine::p4k::Archive::open(fsPath(path));
    if (!archive)
        return out;
    for (std::size_t i = 0; i < (*archive)->entryCount(); ++i) {
        auto bytes = (*archive)->read(i);
        out.insert(QString::fromUtf8((*archive)->name(i)),
                   bytes ? QByteArray(reinterpret_cast<const char *>(bytes->data()), qsizetype(bytes->size()))
                         : QByteArray());
    }
    return out;
}

void writeZip(const QString &path, const QList<std::pair<const char *, QByteArray>> &entries)
{
    auto zip = engine::zip::ZipWriter::create(fsPath(path));
    if (!zip)
        qFatal("cannot create zip");
    for (const auto &[name, bytes] : entries)
        if (!zip->add(name, std::string_view(bytes.constData(), bytes.size())))
            qFatal("cannot add");
    if (!zip->finish())
        qFatal("cannot finish");
}

const QByteArray kManifest = R"({"app": "SmartCitizen", "kind": "settings-backup", "schema_version": 1})";

// A one-request-per-connection HTTP server driven by the test's event loop
// (downloadIfChanged spins a nested one).
class TinyHttp : public QObject
{
public:
    using Responder = std::function<QByteArray(const QByteArray &request)>;

    explicit TinyHttp(Responder responder) : responder_(std::move(responder))
    {
        server_.listen(QHostAddress::LocalHost);
        connect(&server_, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = server_.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    buffers_[socket] += socket->readAll();
                    if (!buffers_[socket].contains("\r\n\r\n"))
                        return;
                    requests << buffers_.take(socket);
                    const QByteArray response = responder_(requests.back());
                    if (response.isNull())
                        return; // hang: never answer
                    socket->write(response);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    QUrl url(const char *path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(server_.serverPort()).arg(s(path)));
    }

    QList<QByteArray> requests;

private:
    QTcpServer server_;
    Responder responder_;
    QHash<QTcpSocket *, QByteArray> buffers_;
};

QByteArray response(int status, const QByteArray &body,
                    const QByteArray &headers = "Content-Type: text/plain\r\n")
{
    return "HTTP/1.1 " + QByteArray::number(status) + " X\r\n" + headers +
           "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

} // namespace

class TestServices : public QObject
{
    Q_OBJECT

private slots:
    // ── settings backups ────────────────────────────────────────────────

    void profileRoundTrip()
    {
        QTemporaryDir dir;
        const QString zip = dir.filePath(s("backup.zip"));
        const QVariantMap settings = {{s("theme"), s("dark")},
                                      {s("favorite_prefix"), s("★")},
                                      {s("merge_hierarchy"), QStringList{s("global"), s("user")}}};
        const QMap<QString, QString> overrides = {{s("LIVE"), s("k=Ærøskøbing — ✓\n")},
                                                  {s("PTU"), s("a=b\n")}};
        const auto written =
            profile::writeProfileZip(zip, settings, overrides, s("0.1.0"), profile::kSourcePortable,
                                     QDateTime(QDate(2026, 7, 24), QTime(9, 30)));
        QCOMPARE(written.value_or(-1), 4);
        QCOMPARE(QStringList(zipEntries(zip).keys()),
                 (QStringList{s("manifest.json"), s("overrides/LIVE/user.ini"), s("overrides/PTU/user.ini"),
                              s("settings.json")}));
        const QJsonObject manifest =
            QJsonDocument::fromJson(zipEntries(zip).value(s("manifest.json"))).object();
        QCOMPARE(manifest.value(s("channels")).toVariant().toStringList(),
                 (QStringList{s("LIVE"), s("PTU")}));
        QCOMPARE(manifest.value(s("exported_at")).toString(), s("2026-07-24T09:30:00"));

        const auto read = profile::readProfileZip(zip);
        QVERIFY2(read.has_value(), qPrintable(read ? QString() : read.error()));
        QCOMPARE(read->overrides, overrides);
        QCOMPARE(read->settings.value(s("favorite_prefix")).toString(), s("★"));
        QCOMPARE(read->settings.value(s("merge_hierarchy")).toStringList(),
                 (QStringList{s("global"), s("user")}));
        QCOMPARE(read->appVersion, s("0.1.0"));
        QCOMPARE(read->sourceMode, profile::kSourcePortable);
        QCOMPARE(read->schemaVersion, profile::kSchemaVersion);

        QVERIFY(
            profile::writeProfileZip(zip, settings, {}, s("0.1.0"), profile::kSourceInstalled).has_value());
        QVERIFY(profile::readProfileZip(zip)->overrides.isEmpty());
    }

    void readsSmartCitizenBackups()
    {
        const auto read = profile::readProfileZip(
            QStringLiteral(SC_SOURCE_DIR "/tests/fixtures/smartcitizen_settings_backup.zip"));
        QVERIFY2(read.has_value(), qPrintable(read ? QString() : read.error()));
        QCOMPARE(read->appVersion, s("2.3.1"));
        QCOMPARE(read->sourceMode, s("registry"));
        QCOMPARE(read->exportedAt, s("2026-07-24T09:30:00"));
        QCOMPARE(read->settings.value(s("favorite_prefix")).toString(), s("★"));
        QCOMPARE(read->settings.value(s("data_sources/global/enabled")).toBool(), true);
        QCOMPARE(read->settings.value(s("count")).toInt(), 3);
        QCOMPARE(read->overrides.value(s("LIVE")), s("item_NameFoo=Ærøskøbing — ✓\n"));
        QCOMPARE(read->overrides.keys(), (QStringList{s("LIVE"), s("PTU")}));
    }

    void rejectsBadBackups()
    {
        QTemporaryDir dir;
        const auto fails = [&](const char *name, const QList<std::pair<const char *, QByteArray>> &entries) {
            const QString path = dir.filePath(s(name));
            writeZip(path, entries);
            return !profile::readProfileZip(path).has_value();
        };
        writeFile(dir.filePath(s("plain.zip")), "not a zip at all");
        QVERIFY(!profile::readProfileZip(dir.filePath(s("plain.zip"))).has_value());
        QVERIFY(fails("nomanifest.zip", {{"settings.json", "{}"}}));
        QVERIFY(fails("wrongapp.zip", {{"manifest.json", R"({"app": "Other", "schema_version": 1})"}}));
        QVERIFY(fails("newer.zip", {{"manifest.json", R"({"app": "SmartCitizen", "schema_version": 99})"}}));
        QVERIFY(fails("noschema.zip", {{"manifest.json", R"({"app": "SmartCitizen"})"}}));
        QVERIFY(fails("garbled.zip", {{"manifest.json", "{nope"}}));
        QVERIFY(fails("badsettings.zip", {{"manifest.json", kManifest}, {"settings.json", "{nope"}}));
        QVERIFY(fails("listsettings.zip", {{"manifest.json", kManifest}, {"settings.json", "[1, 2]"}}));

        const QString unsafe = dir.filePath(s("unsafe.zip"));
        writeZip(unsafe, {{"manifest.json", kManifest},
                          {"overrides/LIVE/user.ini", "good=1\n"},
                          {"overrides/a/b/user.ini", "nested=1\n"},
                          {"overrides/../user.ini", "dots=1\n"},
                          {"overrides/C:evil/user.ini", "drive=1\n"},
                          {"overrides/user.ini", "short=1\n"}});
        const auto read = profile::readProfileZip(unsafe);
        QVERIFY(read.has_value());
        QCOMPARE(read->overrides, (QMap<QString, QString>{{s("LIVE"), s("good=1\n")}}));
    }

    void excludedKeys()
    {
        for (const char *key :
             {"user_data_dir", "UserDataDir", "cache_dir", "pending_cache_cleanup", "window_geometry",
              "window_state", "string_column_widths", "base_global_path", "vehicles_path",
              "last_overrides_path", "post_import/apply_pending", "_channel_layout_migrated"})
            QVERIFY2(profile::isProfileExcludedKey(s(key)), key);
        QVERIFY(profile::isProfileExcludedKey(s("data_sources/global/path"), s("C:\\Users\\x\\base.ini")));
        QVERIFY(!profile::isProfileExcludedKey(s("data_sources/global/path"),
                                               s("https://example.com/global.ini")));
        QVERIFY(!profile::isProfileExcludedKey(s("theme"), s("dark")));
        QVERIFY(!profile::isProfileExcludedKey(s("data_sources/global/enabled"), true));
        QVERIFY(!profile::isProfileExcludedKey(s("sc_install_root"), s("C:/Games/StarCitizen")));
    }

    void exportAndImportValues()
    {
        QTemporaryDir dir;
        Settings st(dir.filePath(s("settings.ini")));
        st.setValue(s("theme"), s("dark"));
        st.setValue(s("cache_dir"), s("D:/fast"));
        st.setValue(s("_marker"), true);
        QVERIFY(profile::exportSettingsValues(st).keys() == QStringList{s("theme")});

        Settings target(dir.filePath(s("target.ini")));
        target.setFavoritePrefix(s("*"));
        QCOMPARE(profile::importSettingsValues(target, {{s("theme"), s("light")},
                                                        {s("user_data_dir"), s("X")},
                                                        {s("_marker"), 1},
                                                        {QString(), s("y")},
                                                        {s("ok"), 1}}),
                 2);
        QCOMPARE(target.value(s("theme")).toString(), s("light"));
        QVERIFY(!target.value(s("user_data_dir")).isValid());
        QCOMPARE(target.favoritePrefix(), s("*")); // layered, not replaced
    }

    void reconcileInstallPath()
    {
        QTemporaryDir dir;
        Settings st(dir.filePath(s("settings.ini")));
        const QString real = dir.filePath(s("RSI/StarCitizen"));
        QDir().mkpath(real + s("/LIVE"));

        st.setScInstallRoot(real);
        QCOMPARE(profile::reconcileImportedInstallPath(st, [] { return QString(); }),
                 profile::InstallPathOutcome::Restored);
        QCOMPARE(st.scInstallRoot(), real);

        st.setScInstallRoot(s("Q:/Nowhere/StarCitizen"));
        QCOMPARE(profile::reconcileImportedInstallPath(st, [&] { return real; }),
                 profile::InstallPathOutcome::Redetected);
        QCOMPARE(st.scInstallRoot(), real);

        st.setScInstallRoot(s("Q:/Nowhere/StarCitizen"));
        st.setValue(s("game_install_path"), s("Q:/Nowhere/StarCitizen/LIVE"));
        QCOMPARE(profile::reconcileImportedInstallPath(st, [] { return QString(); }),
                 profile::InstallPathOutcome::None);
        QVERIFY(st.scInstallRoot().isEmpty());
        QVERIFY(!st.value(s("game_install_path")).isValid());
    }

    void filenames()
    {
        const QString backup = profile::defaultBackupFilename(QDate(2026, 7, 24));
        QVERIFY(backup.endsWith(s("-Settings-Backup-20260724.zip")));
        QVERIFY(!backup.contains(s("0.")));
        QVERIFY(
            defaultLocPackFilename(s("LIVE"), QDate(2026, 5, 9)).endsWith(s("-LocPack-LIVE-20260509.zip")));
        QVERIFY(defaultLocPackFilename(s("PTU")) != defaultLocPackFilename(s("LIVE")));
    }

    // ── loc-packs ───────────────────────────────────────────────────────

    void locPack()
    {
        QTemporaryDir dir;
        QByteArray payload;
        for (int i = 0; i < 2000; ++i)
            payload += "item_Name" + QByteArray::number(i) + "=Some repetitive English text\r\n";
        const QString src = dir.filePath(s("global.ini"));
        writeFile(src, payload);
        const QString zip = dir.filePath(s("pack.zip"));
        writeFile(zip, "old content");
        QCOMPARE(writeLocPackZip(src, zip).value_or(-1), payload.size());
        QCOMPARE(zipEntries(zip), (QMap<QString, QByteArray>{{s("global.ini"), payload}}));
        QVERIFY(QFileInfo(zip).size() < payload.size() / 4);

        const auto missing = writeLocPackZip(dir.filePath(s("nope/global.ini")), zip);
        QVERIFY(!missing.has_value());
        QVERIFY(missing.error().contains(s("not found")));
    }

    // ── downloads ───────────────────────────────────────────────────────

    void downloadThenNotModified()
    {
        TinyHttp http([](const QByteArray &request) {
            if (request.contains("If-None-Match: \"v1\""))
                return response(304, {});
            return response(200, "key=value\n", "Content-Type: text/plain\r\nETag: \"v1\"\r\n");
        });
        QTemporaryDir dir;
        const QString dest = dir.filePath(s("cache/base.ini"));
        auto r = net::downloadIfChanged(http.url("/global.ini"), dest);
        QCOMPARE(r.status, net::DownloadResult::Status::Downloaded);
        QCOMPARE(r.bytes, 10);
        QCOMPARE(readFile(dest), QByteArray("key=value\n"));
        QCOMPARE(readFile(dest + s(".etag")), QByteArray("\"v1\""));
        QVERIFY(http.requests[0].contains("User-Agent: " + net::userAgent()));
        QVERIFY(!http.requests[0].contains("If-Modified-Since"));

        r = net::downloadIfChanged(http.url("/global.ini"), dest);
        QCOMPARE(r.status, net::DownloadResult::Status::Unchanged);
        QVERIFY(r.ok());
        QVERIFY(http.requests[1].contains("If-Modified-Since: "));
        QVERIFY(http.requests[1].contains(" GMT"));
        QCOMPARE(readFile(dest), QByteArray("key=value\n"));
    }

    void downloadRefusesWebPages()
    {
        TinyHttp http([](const QByteArray &request) {
            if (request.startsWith("GET /typed"))
                return response(200, "<p>hi</p>", "Content-Type: text/html; charset=utf-8\r\n");
            return response(200, "\xEF\xBB\xBF  <!DOCTYPE html><html></html>");
        });
        QTemporaryDir dir;
        const QString dest = dir.filePath(s("base.ini"));
        auto r = net::downloadIfChanged(http.url("/typed"), dest);
        QCOMPARE(r.status, net::DownloadResult::Status::Failed);
        QVERIFY(r.error.contains(s("web page")));
        r = net::downloadIfChanged(http.url("/sniffed"), dest);
        QCOMPARE(r.status, net::DownloadResult::Status::Failed);
        QVERIFY(!QFile::exists(dest));
        QVERIFY(!net::looksLikeHtml("text/plain", "key=<html>"));
    }

    void downloadErrors()
    {
        TinyHttp http([](const QByteArray &) { return response(404, "missing"); });
        QTemporaryDir dir;
        auto r = net::downloadIfChanged(http.url("/nope.ini"), dir.filePath(s("x.ini")));
        QCOMPARE(r.status, net::DownloadResult::Status::Failed);
        QCOMPARE(r.httpStatus, 404);
        QVERIFY(r.error.contains(s("404")));
        QVERIFY(!net::downloadIfChanged(QUrl(s("file:///C:/x.ini")), dir.filePath(s("x.ini"))).ok());
    }

    void downloadCancels()
    {
        TinyHttp http([](const QByteArray &) { return QByteArray(); }); // never answers
        QTemporaryDir dir;
        std::atomic<bool> cancel = false;
        QTimer::singleShot(200, [&] { cancel = true; });
        QElapsedTimer t;
        t.start();
        const auto r =
            net::downloadIfChanged(http.url("/slow.ini"), dir.filePath(s("x.ini")), {60'000, &cancel});
        QCOMPARE(r.status, net::DownloadResult::Status::Cancelled);
        QVERIFY(t.elapsed() < 5000);
    }

    // ── logging and crash reports ───────────────────────────────────────

    void logRingAndFile()
    {
        QTemporaryDir dir;
        log::LogHub &hub = log::LogHub::instance();
        const QString file = dir.filePath(s("logs/app.log"));
        writeFile(file, "last session\n");
        hub.install(file, /*debug=*/true);
        QVERIFY(hub.isInstalled());
        QCOMPARE(readFile(file + s(".previous")), QByteArray("last session\n"));
        hub.clearRecent();

        QSignalSpy spy(&hub, &log::LogHub::lineLogged);
        qInfo("hello %d", 42);
        QCOMPARE(spy.size(), 1);
        const QString line = hub.recentLines().constLast();
        QVERIFY(line.endsWith(s(" - default - INFO - hello 42")));
        QVERIFY(QRegularExpression(s(R"(^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d,\d{3} - )")).match(line).hasMatch());
        QVERIFY(readFile(file).contains("INFO - hello 42"));

        for (int i = 0; i < log::kRingSize + 10; ++i)
            hub.record(QtWarningMsg, s("test"), QString::number(i));
        QCOMPARE(hub.recentLines().size(), log::kRingSize);
        QVERIFY(hub.recentLines().constLast().endsWith(s("WARNING - %1").arg(log::kRingSize + 9)));
        hub.install({}, false); // detach the file before the temp dir goes
        QVERIFY(hub.logFile().isEmpty());
    }

    void crashReport()
    {
        QTemporaryDir dir;
        log::LogHub::instance().clearRecent();
        log::LogHub::instance().record(QtCriticalMsg, s("scx.test"), s("the last thing that happened"));
        const QString path = crash::writeCrashReport(dir.filePath(s("logs")),
                                                     s("Unhandled exception 0xc0000005"), s("Worker"));
        QVERIFY(!path.isEmpty());
        QVERIFY(QFileInfo(path).fileName().startsWith(s("crash_")));
        const QString text = QString::fromUtf8(readFile(path));
        QVERIFY(text.contains(s("Thread: Worker")));
        QVERIFY(text.contains(s("Unhandled exception 0xc0000005")));
        QVERIFY(text.contains(s("Recent log (1 lines)")));
        QVERIFY(text.contains(s("ERROR - the last thing that happened")));
        QVERIFY(crash::writeCrashReport(QString(), s("x")).isEmpty());
    }

    // ── progress ────────────────────────────────────────────────────────

    void progressSink()
    {
        ProgressSink quiet;
        quiet.setTotal(3);
        quiet.advance();
        quiet.advance(1, s("b"));
        QCOMPARE(quiet.snapshot(), std::make_tuple(2, 3, s("b")));

        QList<std::tuple<int, int, QString>> events;
        const auto record = [&](int c, int t, const QString &m) { events.append({c, t, m}); };
        ProgressSink latest(record, 0, std::chrono::milliseconds(0));
        latest.setTotal(2);
        latest.advance(1, s("phase1"));
        latest.advance(1, s("phase2"));
        QCOMPARE(events.constLast(), std::make_tuple(2, 2, s("phase2")));

        events.clear();
        ProgressSink throttled(record, 1000, std::chrono::hours(1));
        for (int i = 0; i < 100; ++i)
            throttled.advance();
        QVERIFY(events.size() <= 2);
        throttled.advance(900, s("done"));
        QCOMPARE(events.constLast(), std::make_tuple(1000, 1000, s("done")));

        events.clear();
        ProgressSink forced(record, 0, std::chrono::hours(1));
        forced.setTotal(5);
        QCOMPARE(events.constLast(), std::make_tuple(0, 5, QString()));
    }

    void progressSinkThreads()
    {
        ProgressSink sink({}, 1000);
        QList<QThread *> threads;
        for (int t = 0; t < 10; ++t)
            threads << QThread::create([&] {
                for (int i = 0; i < 100; ++i)
                    sink.advance();
            });
        for (QThread *t : threads)
            t->start();
        for (QThread *t : threads) {
            t->wait();
            delete t;
        }
        QCOMPARE(std::get<0>(sink.snapshot()), 1000);

        CancelToken token;
        const CancelToken copy = token;
        QVERIFY(!copy.isCancelled());
        token.cancel();
        QVERIFY(copy.isCancelled() && copy.flag()->load());
        {
            PerfTimer timer("tst");
        }
    }
};

QTEST_GUILESS_MAIN(TestServices)
#include "tst_services.moc"
