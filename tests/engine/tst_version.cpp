#include "engine/Version.h"

#include <QTest>

class TestEngineVersion : public QObject
{
    Q_OBJECT

private slots:
    void linksPinnedLibraries()
    {
        const engine::LibraryVersions v = engine::libraryVersions();
        QCOMPARE(QString::fromStdString(v.zstd), QStringLiteral("1.5.7"));
        QCOMPARE(QString::fromStdString(v.zlib), QStringLiteral("1.3.1"));
        QCOMPARE(QString::fromStdString(v.pugixml), QStringLiteral("1.15"));
    }
};

QTEST_APPLESS_MAIN(TestEngineVersion)
#include "tst_version.moc"
