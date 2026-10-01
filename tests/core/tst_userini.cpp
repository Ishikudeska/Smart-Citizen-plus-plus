// Ports test_user_ini_backups.py, test_user_ini_reset.py,
// test_user_ini_autosave_guard.py, test_user_data_dir_migration.py and
// test_favorite_prefix_whitespace.py.

#include "core/user/UserIni.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace core;

namespace {

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        qFatal("cannot open %s", qPrintable(f.fileName()));
    f.write(bytes);
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("cannot open %s", qPrintable(f.fileName()));
    return f.readAll();
}

StringEntry entry(const char *key, const char *original, const char *custom)
{
    StringEntry e;
    e.key = QString::fromUtf8(key);
    e.originalValue = QString::fromUtf8(original);
    e.customValue = QString::fromUtf8(custom);
    return e;
}

} // namespace

class TestUserIni : public QObject
{
    Q_OBJECT

private slots:
    void savesOnlyModifiedEntriesVerbatim()
    {
        QTemporaryDir dir;
        const UserIni ini(dir.filePath(QStringLiteral("LIVE/user.ini")));
        const QList<StringEntry> entries = {
            entry("b_key", "Orig", "Edited"),
            entry("same", "Same", "Same"),          // not an edit
            entry("none", "Value", ""),             // not an edit
            entry("vehicle_NameX", "Cutlass", " Cutlass"), // space favourite prefix (#100)
        };
        QCOMPARE(ini.save(entries).value_or(-1), 2);
        QCOMPARE(readAll(ini.path()), QByteArray("b_key=Edited\r\nvehicle_NameX= Cutlass\r\n"));
        QCOMPARE(ini.load().value(QStringLiteral("vehicle_NameX")), QStringLiteral(" Cutlass"));
    }

    void noBackupOnFirstSaveOrIdenticalSave()
    {
        QTemporaryDir dir;
        const UserIni ini(dir.filePath(QStringLiteral("user.ini")));
        IniMap m;
        m.insert(QStringLiteral("k"), QStringLiteral("v"));
        QVERIFY(ini.save(m));
        QVERIFY(ini.backups().isEmpty());
        QVERIFY(ini.save(m)); // same content, CRLF on disk vs LF in memory
        QVERIFY(ini.backups().isEmpty());
        m.insert(QStringLiteral("k"), QStringLiteral("changed"));
        QVERIFY(ini.save(m));
        QCOMPARE(ini.backups().size(), 1);
        QCOMPARE(readAll(ini.backups().first().absoluteFilePath()), QByteArray("k=v\r\n"));
    }

    void truncatingSaveSnapshotsPopulatedFile()
    {
        QTemporaryDir dir;
        const UserIni ini(dir.filePath(QStringLiteral("user.ini")));
        writeFile(ini.path(), "a=1\r\nb=2\r\n");
        QVERIFY(ini.save(IniMap()));
        QCOMPARE(readAll(ini.path()), QByteArray());
        QCOMPARE(readAll(ini.backups().first().absoluteFilePath()), QByteArray("a=1\r\nb=2\r\n"));
    }

    void corruptFileIsSnapshottedBeforeRewrite()
    {
        QTemporaryDir dir;
        const UserIni ini(dir.filePath(QStringLiteral("user.ini")));
        writeFile(ini.path(), "key=broken\xA0value\n");
        IniMap m;
        m.insert(QStringLiteral("key"), QStringLiteral("fixed"));
        QVERIFY(ini.save(m));
        QCOMPARE(ini.backups().size(), 1);
    }

    void backupRotationAndOrder()
    {
        QTemporaryDir dir;
        const UserIni ini(dir.filePath(QStringLiteral("user.ini")));
        QVERIFY(ini.backup().isEmpty()); // missing
        writeFile(ini.path(), "");
        QVERIFY(ini.backup().isEmpty()); // empty
        writeFile(ini.path(), "a=1\r\n");

        const QDateTime t0(QDate(2026, 3, 1), QTime(10, 0, 0));
        for (int i = 0; i < 7; ++i)
            QVERIFY(!ini.backup(t0.addSecs(i)).isEmpty());
        const QFileInfoList list = ini.backups();
        QCOMPARE(list.size(), UserIni::kKeepBackups);
        QVERIFY(list.first().fileName().startsWith(QStringLiteral("user.ini.bak_20260301_100006_")));
        QVERIFY(list.last().fileName().startsWith(QStringLiteral("user.ini.bak_20260301_100002_")));

        // Same timestamp twice: the second gets a suffix instead of clobbering.
        const QString again1 = ini.backup(t0.addSecs(6));
        QVERIFY(again1.endsWith(QStringLiteral("-2")));
    }

    void restoreIsReversible()
    {
        QTemporaryDir dir;
        const UserIni ini(dir.filePath(QStringLiteral("user.ini")));
        writeFile(ini.path(), "v=old\r\n");
        const QString oldBackup = ini.backup(QDateTime(QDate(2026, 1, 1), QTime(1, 0)));
        writeFile(ini.path(), "v=new\r\n");
        QVERIFY(ini.restore(oldBackup));
        QCOMPARE(readAll(ini.path()), QByteArray("v=old\r\n"));
        // The pre-restore state was snapshotted.
        bool found = false;
        for (const QFileInfo &b : ini.backups())
            found = found || readAll(b.absoluteFilePath()) == "v=new\r\n";
        QVERIFY(found);
    }

    void resetRenamesOrDeletes()
    {
        QTemporaryDir dir;
        const UserIni ini(dir.filePath(QStringLiteral("user.ini")));
        QVERIFY(ini.reset().isEmpty()); // absent

        const QDateTime now(QDate(2026, 5, 6), QTime(7, 8, 9));
        writeFile(ini.path(), "a=1\r\n");
        const QString first = ini.reset(true, now);
        QCOMPARE(QFileInfo(first).fileName(), QStringLiteral("user.ini.bak-20260506-070809"));
        QVERIFY(!QFileInfo::exists(ini.path()));

        writeFile(ini.path(), "b=2\r\n");
        const QString second = ini.reset(true, now);
        QCOMPARE(QFileInfo(second).fileName(), QStringLiteral("user.ini.bak-20260506-070809-2"));
        QCOMPARE(readAll(first), QByteArray("a=1\r\n"));

        writeFile(ini.path(), "c=3\r\n");
        QVERIFY(ini.reset(false).isEmpty());
        QVERIFY(!QFileInfo::exists(ini.path()));
    }

    void autosaveGuard()
    {
        QTemporaryDir dir;
        const UserIni ini(dir.filePath(QStringLiteral("user.ini")));
        const QList<StringEntry> untouched = {entry("a", "x", ""), entry("b", "y", "y")};
        const QList<StringEntry> edited = {entry("a", "x", "z")};
        QVERIFY(ini.shouldAutosave(untouched)); // nothing on disk
        writeFile(ini.path(), "");
        QVERIFY(ini.shouldAutosave(untouched)); // empty on disk
        writeFile(ini.path(), "a=z\r\n");
        QVERIFY(!ini.shouldAutosave(untouched)); // would truncate real edits
        QVERIFY(!ini.shouldAutosave({}));
        QVERIFY(ini.shouldAutosave(edited));
    }

    void generatesFromDiff()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("base.ini")), "a=1\r\nb=2\r\nc=3\r\n");
        writeFile(dir.filePath(QStringLiteral("global.ini")), "\xEF\xBB\xBF" "a=1\r\nb= *Two\r\nnew=x\r\n");
        const UserIni ini(dir.filePath(QStringLiteral("user.ini")));
        QCOMPARE(ini.generateFromDiff(dir.filePath(QStringLiteral("base.ini")), dir.filePath(QStringLiteral("global.ini"))),
                 2);
        QCOMPARE(readAll(ini.path()), QByteArray("b= *Two\r\nnew=x\r\n"));
        // Never over an existing user.ini.
        QCOMPARE(ini.generateFromDiff(dir.filePath(QStringLiteral("base.ini")), dir.filePath(QStringLiteral("global.ini"))),
                 0);
    }

    void migrationCopiesWithoutOverwriting()
    {
        QTemporaryDir dir;
        const QString oldRoot = dir.filePath(QStringLiteral("old"));
        const QString newRoot = dir.filePath(QStringLiteral("new"));
        writeFile(oldRoot + QStringLiteral("/LIVE/user.ini"), "old");
        writeFile(oldRoot + QStringLiteral("/LIVE/cache/base.ini"), "base");
        writeFile(newRoot + QStringLiteral("/LIVE/user.ini"), "already here");

        QCOMPARE(migrateUserDataDir(oldRoot, newRoot), 1);
        QCOMPARE(readAll(newRoot + QStringLiteral("/LIVE/user.ini")), QByteArray("already here"));
        QCOMPARE(readAll(newRoot + QStringLiteral("/LIVE/cache/base.ini")), QByteArray("base"));
        QVERIFY(QFileInfo::exists(oldRoot + QStringLiteral("/LIVE/cache/base.ini"))); // copy keeps originals

        QCOMPARE(migrateUserDataDir(oldRoot, oldRoot), 0);
        QCOMPARE(migrateUserDataDir(dir.filePath(QStringLiteral("missing")), newRoot), 0);
    }

    void migrationMovePrunesAndKeepsConflicts()
    {
        QTemporaryDir dir;
        const QString oldRoot = dir.filePath(QStringLiteral("old"));
        const QString newRoot = dir.filePath(QStringLiteral("new"));
        writeFile(oldRoot + QStringLiteral("/LIVE/cache/base.ini"), "base");
        writeFile(oldRoot + QStringLiteral("/LIVE/user.ini"), "old");
        writeFile(newRoot + QStringLiteral("/LIVE/user.ini"), "new");

        QCOMPARE(migrateUserDataDir(oldRoot, newRoot, true), 1);
        QVERIFY(!QFileInfo::exists(oldRoot + QStringLiteral("/LIVE/cache"))); // emptied dir pruned
        QCOMPARE(readAll(oldRoot + QStringLiteral("/LIVE/user.ini")), QByteArray("old")); // conflict left in place
        QCOMPARE(readAll(newRoot + QStringLiteral("/LIVE/cache/base.ini")), QByteArray("base"));
    }

    void migrationIntoNestedFolderTerminates()
    {
        QTemporaryDir dir;
        const QString oldRoot = dir.filePath(QStringLiteral("data"));
        const QString newRoot = oldRoot + QStringLiteral("/nested");
        writeFile(oldRoot + QStringLiteral("/LIVE/user.ini"), "x");
        QCOMPARE(migrateUserDataDir(oldRoot, newRoot), 1);
        QVERIFY(QFileInfo::exists(newRoot + QStringLiteral("/LIVE/user.ini")));
        QVERIFY(!QFileInfo::exists(newRoot + QStringLiteral("/nested")));
    }
};

QTEST_GUILESS_MAIN(TestUserIni)
#include "tst_userini.moc"
