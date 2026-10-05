// Core parity against the original Python (plan P5): the applied
// global.ini must be byte-equal to what Smart Citizen writes for the same
// base.ini, enhancement INIs and user.ini.
//
//   SCX_SC_CACHE  folder holding Smart Citizen's base.ini and enhancement
//                 INIs, default %USERPROFILE%\Documents\Smart Citizen\LIVE\cache
//                 (its user.ini is taken from the parent folder)
// Needs the reference checkout ("Smart Citizen CPLusPLus/", git-ignored) and
// a Python with PyQt6; skips otherwise.

#include "core/apply/ApplyService.h"
#include "core/merge/SourceLoader.h"
#include "core/model/Enhancements.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace core;

class TestParityCore : public QObject
{
    Q_OBJECT

private slots:
    void p5ApplyMatchesPython()
    {
        const QString reference = QStringLiteral(SC_SOURCE_DIR "/Smart Citizen CPLusPLus");
        const QString cache = qEnvironmentVariable(
            "SCX_SC_CACHE", QDir::homePath() + QStringLiteral("/Documents/Smart Citizen/LIVE/cache"));
        const QString python = QStandardPaths::findExecutable(QStringLiteral("python"));
        if (!QFileInfo::exists(reference + QStringLiteral("/src/merger/ini_merger.py")))
            QSKIP("reference checkout not present");
        if (!QFileInfo::exists(cache + QStringLiteral("/base.ini")))
            QSKIP("no Smart Citizen cache (SCX_SC_CACHE)");
        if (python.isEmpty())
            QSKIP("python not on PATH");

        const QString userIni = QFileInfo(cache).dir().filePath(QStringLiteral("user.ini"));
        const QString version = QStringLiteral("9.9.9");
        QTemporaryDir out;

        QProcess py;
        QStringList args = {QStringLiteral(SC_SOURCE_DIR "/tools/parity/apply_python.py"),
                            QStringLiteral("--reference"),
                            reference,
                            QStringLiteral("--cache"),
                            cache,
                            QStringLiteral("--version"),
                            version,
                            QStringLiteral("--out"),
                            out.filePath(QStringLiteral("python.ini"))};
        if (QFileInfo::exists(userIni))
            args << QStringLiteral("--user-ini") << userIni;
        py.start(python, args);
        QVERIFY(py.waitForFinished(300000));
        QVERIFY2(py.exitCode() == 0, py.readAllStandardError().constData());

        SourceFiles files;
        files.baseIni = cache + QStringLiteral("/base.ini");
        files.enhancementsDir = cache;
        for (const auto &f : enhancements::files())
            files.enhancementFileIds << QString::fromLatin1(f.id);
        files.userIni = userIni;

        ApplyInputs in;
        in.sources = loadSources(files);
        const auto user = in.sources.sources.find(kSourceUser);
        if (user != in.sources.sources.end())
            for (const auto &[key, value] : user->second)
                if (!value.isEmpty())
                    in.userOverrides.insert(key, value);
        in.baseIniPath = files.baseIni;
        in.gameFile = out.filePath(QStringLiteral("cpp.ini"));
        in.backupsDir = out.filePath(QStringLiteral("backups"));
        in.appName = QStringLiteral("Smart Citizen");
        in.version = version;
        const ApplyOutcome outcome = applyToGame(in);
        QVERIFY2(outcome.ok, qPrintable(outcome.error + outcome.validation));

        QFile a(out.filePath(QStringLiteral("python.ini"))), b(in.gameFile);
        QVERIFY(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
        const QByteArray expected = a.readAll(), actual = b.readAll();
        if (expected != actual) {
            const QList<QByteArray> el = expected.split('\n'), al = actual.split('\n');
            for (qsizetype i = 0; i < std::min(el.size(), al.size()); ++i)
                if (el[i] != al[i]) {
                    qWarning("line %lld differs:\n  python: %s\n  c++:    %s", i + 1,
                             el[i].left(300).constData(), al[i].left(300).constData());
                    break;
                }
        }
        qInfo("applied global.ini: %lld bytes", static_cast<long long>(actual.size()));
        QCOMPARE(actual.size(), expected.size());
        QVERIFY(actual == expected);
    }
};

QTEST_GUILESS_MAIN(TestParityCore)
#include "tst_parity_core.moc"
