#include "core/AppIdentity.h"
#include "core/net/AppUpdater.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace core::net;

// Mirrors tests/test_app_updater.py's cases for the pure helpers.
class TestAppUpdater : public QObject
{
    Q_OBJECT

private slots:
    void parsesVersions()
    {
        QCOMPARE(parseVersion(QStringLiteral("2.3.1")), (Version{2, 3, 1}));
        QCOMPARE(parseVersion(QStringLiteral("v2.3")), (Version{2, 3, 0}));
        QCOMPARE(parseVersion(QStringLiteral(" V10.0.7 ")), (Version{10, 0, 7}));
        QCOMPARE(parseVersion(QStringLiteral("1.2.3.4")), (Version{1, 2, 3}));
        QVERIFY(!parseVersion(QStringLiteral("")));
        QVERIFY(!parseVersion(QStringLiteral("2")));
        QVERIFY(!parseVersion(QStringLiteral("v2.x")));
        QVERIFY(!parseVersion(QStringLiteral("2.3.1-beta")));
    }

    void comparesVersions()
    {
        QVERIFY(isNewer(QStringLiteral("v2.3.2"), QStringLiteral("2.3.1")));
        QVERIFY(isNewer(QStringLiteral("2.10.0"), QStringLiteral("2.9.9")));
        QVERIFY(!isNewer(QStringLiteral("2.3.1"), QStringLiteral("2.3.1")));
        QVERIFY(!isNewer(QStringLiteral("2.3.0"), QStringLiteral("2.3.1")));
        // Garbage on either side never prompts.
        QVERIFY(!isNewer(QStringLiteral("nightly"), QStringLiteral("2.3.1")));
        QVERIFY(!isNewer(QStringLiteral("3.0.0"), QStringLiteral("dev")));
    }

    void picksInstallerAsset()
    {
        const QString hex(64, u'a');
        const QString digest = QStringLiteral("sha256:") + hex;
        const auto asset = [&](const char *name, const char *url, int size, const QString &d = {}) {
            return QJsonObject{{"name", name},
                               {"browser_download_url", url},
                               {"size", size},
                               {"digest", d.isNull() ? digest : d}};
        };
        const QJsonArray assets{
            asset("SCX-Portable-1.0.0.zip", "https://x/portable.zip", 10),
            asset("SCX-1.0.0-Setup.exe.sha256", "https://x/sum", 1),
            asset("scx-1.0.0-setup.exe", "https://x/setup.exe", 1234,
                  QStringLiteral("SHA256:") + hex.toUpper()),
        };
        const auto picked = pickInstallerAsset(assets, QStringLiteral("SCX"));
        QVERIFY(picked);
        QCOMPARE(picked->url, QUrl(QStringLiteral("https://x/setup.exe")));
        QCOMPARE(picked->size, 1234);
        QCOMPARE(picked->sha256, hex.toLatin1());

        QVERIFY(!pickInstallerAsset(QJsonArray{asset("Other-1.0-Setup.exe", "https://x/o.exe", 1)},
                                    QStringLiteral("SCX")));
        QVERIFY(!pickInstallerAsset(QJsonArray{asset("SCX-1.0-Setup.exe", "", 1)}, QStringLiteral("SCX")));
        QVERIFY(!pickInstallerAsset(QJsonArray{QJsonValue(3)}, QStringLiteral("SCX")));
        QVERIFY(!pickInstallerAsset(QJsonArray{}, QStringLiteral("SCX")));
        // Never an installer that cannot be verified, or one fetched in the clear.
        QVERIFY(!pickInstallerAsset(QJsonArray{asset("SCX-1.0-Setup.exe", "http://x/s.exe", 1)},
                                    QStringLiteral("SCX")));
        QVERIFY(!pickInstallerAsset(QJsonArray{asset("SCX-1.0-Setup.exe", "https://x/s.exe", 1, QString(""))},
                                    QStringLiteral("SCX")));
        QVERIFY(!pickInstallerAsset(
            QJsonArray{asset("SCX-1.0-Setup.exe", "https://x/s.exe", 1, QStringLiteral("sha256:abc"))},
            QStringLiteral("SCX")));
        QVERIFY(!pickInstallerAsset(
            QJsonArray{asset("SCX-1.0-Setup.exe", "https://x/s.exe", 1, QStringLiteral("md5:") + hex)},
            QStringLiteral("SCX")));
    }

    // The download is kept only when its size and SHA-256 match the release.
    void verifiesDownloadedInstaller()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray body = "pretend installer bytes";
        const QString source = dir.filePath(QStringLiteral("SCX-1.0-Setup.exe"));
        {
            QFile f(source);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(body);
        }
        InstallerAsset asset{QUrl::fromLocalFile(source), body.size(),
                             QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex()};
        const QString dest = dir.filePath(QStringLiteral("out"));
        const QString target = QDir(dest).filePath(QStringLiteral("SCX-1.0-Setup.exe"));

        QString error;
        QCOMPARE(downloadInstaller(asset, dest, &error), target);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QFile got(target);
        QVERIFY(got.open(QIODevice::ReadOnly));
        QCOMPARE(got.readAll(), body);
        got.close();

        const auto rejects = [&](const InstallerAsset &bad) {
            QVERIFY(QFile::remove(target));
            QString message;
            QVERIFY(downloadInstaller(bad, dest, &message).isEmpty());
            QVERIFY(!message.isEmpty());
            QVERIFY(!QFileInfo::exists(target));
            QVERIFY(!QFileInfo::exists(target + QStringLiteral(".part")));
            QFile::copy(source, target); // restore for the next case
        };
        InstallerAsset wrongHash = asset;
        wrongHash.sha256 = QByteArray(64, 'a');
        rejects(wrongHash);
        InstallerAsset wrongSize = asset;
        wrongSize.size = body.size() + 1;
        rejects(wrongSize);
        InstallerAsset noHash = asset;
        noHash.sha256.clear();
        rejects(noHash);
    }

    void apiAddress()
    {
        QCOMPARE(latestReleaseApi(QStringLiteral("owner/repo")),
                 QUrl(QStringLiteral("https://api.github.com/repos/owner/repo/releases/latest")));
        QVERIFY(latestReleaseApi(QStringLiteral(" ")).isEmpty());
        QCOMPARE(checkForUpdate(QString(), QStringLiteral("1.0.0")).status, UpdateCheck::Status::Disabled);
    }

    // Opt-in (SCX_NETWORK_TESTS=1): the configured repository answers. A repo
    // with no release yet counts as up to date.
    void liveRepository()
    {
        if (qEnvironmentVariable("SCX_NETWORK_TESTS") != u"1")
            QSKIP("set SCX_NETWORK_TESTS=1 to query GitHub");
        const QString repo = QString::fromLatin1(core::identity::kUpdateRepo);
        if (repo.isEmpty())
            QSKIP("no update repository configured");
        const UpdateCheck check = checkForUpdate(repo, QStringLiteral("0.0.1"));
        QVERIFY2(check.status != UpdateCheck::Status::Failed, qPrintable(check.error));
        qInfo() << "latest release:" << (check.latest.isEmpty() ? QStringLiteral("(none)") : check.latest);
    }
};

QTEST_GUILESS_MAIN(TestAppUpdater)
#include "tst_appupdater.moc"
