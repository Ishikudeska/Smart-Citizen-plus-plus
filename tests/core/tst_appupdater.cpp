#include "core/net/AppUpdater.h"

#include <QJsonArray>
#include <QJsonObject>
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
        const auto asset = [](const char *name, const char *url, int size) {
            return QJsonObject{{"name", name}, {"browser_download_url", url}, {"size", size}};
        };
        const QJsonArray assets{
            asset("SCX-Portable-1.0.0.zip", "https://x/portable.zip", 10),
            asset("SCX-1.0.0-Setup.exe.sha256", "https://x/sum", 1),
            asset("scx-1.0.0-setup.exe", "https://x/setup.exe", 1234),
        };
        const auto picked = pickInstallerAsset(assets, QStringLiteral("SCX"));
        QVERIFY(picked);
        QCOMPARE(picked->url, QUrl(QStringLiteral("https://x/setup.exe")));
        QCOMPARE(picked->size, 1234);

        QVERIFY(!pickInstallerAsset(QJsonArray{asset("Other-1.0-Setup.exe", "https://x/o.exe", 1)}, QStringLiteral("SCX")));
        QVERIFY(!pickInstallerAsset(QJsonArray{asset("SCX-1.0-Setup.exe", "", 1)}, QStringLiteral("SCX")));
        QVERIFY(!pickInstallerAsset(QJsonArray{QJsonValue(3)}, QStringLiteral("SCX")));
        QVERIFY(!pickInstallerAsset(QJsonArray{}, QStringLiteral("SCX")));
    }

    void apiAddress()
    {
        QCOMPARE(latestReleaseApi(QStringLiteral("owner/repo")),
                 QUrl(QStringLiteral("https://api.github.com/repos/owner/repo/releases/latest")));
        QVERIFY(latestReleaseApi(QStringLiteral(" ")).isEmpty());
        QCOMPARE(checkForUpdate(QString(), QStringLiteral("1.0.0")).status, UpdateCheck::Status::Disabled);
    }
};

QTEST_GUILESS_MAIN(TestAppUpdater)
#include "tst_appupdater.moc"
