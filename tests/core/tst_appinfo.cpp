#include "core/AppIdentity.h"
#include "core/AppInfo.h"

#include <QCoreApplication>
#include <QTest>

class TestAppInfo : public QObject
{
    Q_OBJECT

private slots:
    void identityIsApplied()
    {
        core::applyIdentity();
        QCOMPARE(QCoreApplication::applicationName(), QString::fromLatin1(core::identity::kAppName));
        QCOMPARE(QCoreApplication::organizationName(), QString::fromLatin1(core::identity::kOrgName));
        QCOMPARE(QCoreApplication::applicationVersion(), QString::fromLatin1(core::identity::kVersion));
    }

    void buildDescriptionNamesLibraries()
    {
        const QString d = core::buildDescription();
        QVERIFY2(d.startsWith(QString::fromLatin1(core::identity::kAppName)), qPrintable(d));
        QVERIFY2(d.contains(QStringLiteral("zstd 1.5.7")), qPrintable(d));
        QVERIFY2(d.contains(QStringLiteral("pugixml 1.15")), qPrintable(d));
    }
};

QTEST_GUILESS_MAIN(TestAppInfo)
#include "tst_appinfo.moc"
