// Ports the merge_ini_files cases from test_ini_merger.py /
// test_ini_encoding_fallback.py, test_frontend_version_stamp.py,
// test_applied_file_validator.py, test_user_cfg.py and the apply pipeline
// (MainWindow.apply_to_game) against a fake Star Citizen folder.

#include "core/apply/ApplyService.h"
#include "core/apply/Backups.h"
#include "core/apply/GameFile.h"
#include "core/apply/Stamps.h"
#include "core/apply/UserCfg.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace core;

namespace {

const QByteArray kBom("\xEF\xBB\xBF");
const QString kApp = QStringLiteral("Smart Citizen");

IniMap ini(std::initializer_list<std::pair<const char *, const char *>> pairs)
{
    IniMap m;
    for (const auto &[k, v] : pairs)
        m.insert(QString::fromUtf8(k), QString::fromUtf8(v));
    return m;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("cannot open %s", qPrintable(f.fileName()));
    return f.readAll();
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        qFatal("cannot open %s", qPrintable(f.fileName()));
    f.write(bytes);
}

QString frontend(const IniMap &m)
{
    return m.value(kFrontendVersionKey);
}

} // namespace

class TestApply : public QObject
{
    Q_OBJECT

private slots:
    // --- game file ------------------------------------------------------------

    void gameFileKeepsStructureAddsBomAndCrlf()
    {
        const QByteArray out =
            renderGameFile(QStringLiteral("a=1\nb=2\n; a comment\n\nc=3\n"), ini({{"b", "overridden"}}));
        QCOMPARE(out, kBom + QByteArray("a=1\r\nb=overridden\r\n; a comment\r\n\r\nc=3\r\n"));
    }

    void gameFileTrailingNewlineShape()
    {
        QCOMPARE(renderGameFile(QStringLiteral("a=1\nb=2"), {}), kBom + QByteArray("a=1\r\nb=2"));
        QCOMPARE(renderGameFile(QStringLiteral("a=1\r\nb=2\r\n"), {}), kBom + QByteArray("a=1\r\nb=2\r\n"));
        QCOMPARE(renderGameFile(QStringLiteral("a=1\rb=2\r"), {}), kBom + QByteArray("a=1\r\nb=2\r\n"));
    }

    void gameFileStripsKeyMetadataAndKeepsRawValues()
    {
        // Keys lose ",P" and their padding; unmatched values stay byte-for-byte.
        const QByteArray out = renderGameFile(
            QStringLiteral(" key,P =  value with spaces  \nother,P=x\nno equals\n"), ini({{"other", "y"}}));
        QCOMPARE(out, kBom + QByteArray("key=  value with spaces  \r\nother=y\r\nno equals\r\n"));
    }

    void gameFileNeverAddsKeys()
    {
        const QByteArray out = renderGameFile(QStringLiteral("a=1\n"), ini({{"a", "2"}, {"new_key", "x"}}));
        QCOMPARE(out, kBom + QByteArray("a=2\r\n"));
    }

    void gameFileFromBomSourceAndCorruptUtf8()
    {
        QTemporaryDir dir;
        const QString base = dir.filePath(QStringLiteral("base.ini"));
        writeFile(base, kBom + QByteArray("FirstKey=first\nbad=x\xA0y\n") + QStringLiteral("é=ü\n").toUtf8());
        const QString out = dir.filePath(QStringLiteral("game/global.ini"));
        QVERIFY(writeGameFile(base, ini({{"FirstKey", "overridden"}}), out).isEmpty());
        QCOMPARE(readAll(out), kBom + QByteArray("FirstKey=overridden\r\nbad=x\xEF\xBF\xBDy\r\n") +
                                   QStringLiteral("é=ü\r\n").toUtf8());
    }

    // --- stamps ---------------------------------------------------------------

    void frontendStamp()
    {
        IniMap m = ini({{"Frontend_PU_Version", "Star Citizen Alpha 4.8.0 PTU"}});
        stampFrontendVersion(m, kApp, QStringLiteral("1.3.1"));
        const QString once = frontend(m);
        QCOMPARE(once,
                 QStringLiteral(
                     R"(Star Citizen Alpha 4.8.0 PTU\nLocalizations Enhanced with Smart Citizen v1.3.1)"));
        stampFrontendVersion(m, kApp, QStringLiteral("1.3.1"));
        QCOMPARE(frontend(m), once); // idempotent
        stampFrontendVersion(m, kApp, QStringLiteral("2.0.0"));
        QCOMPARE(frontend(m),
                 QStringLiteral(
                     R"(Star Citizen Alpha 4.8.0 PTU\nLocalizations Enhanced with Smart Citizen v2.0.0)"));

        IniMap missing = ini({{"Other", "x"}});
        stampFrontendVersion(missing, kApp, QStringLiteral("1"));
        QVERIFY(!missing.contains(kFrontendVersionKey));
    }

    void frontendStampStripsLegacyForms_data()
    {
        QTest::addColumn<QString>("suffix");
        QTest::newRow("heart") << "| Enhanced with <3 by Smart Citizen v1.3.0";
        QTest::newRow("heart no v") << "| Enhanced with <3 by Smart Citizen 1.2.9";
        QTest::newRow("enhanced by") << "| Localizations Enhanced by Smart Citizen 1.3.0";
        QTest::newRow("enhanced by v") << "| Localizations Enhanced by Smart Citizen v1.3.0";
        QTest::newRow("pipe form") << "| Localizations Enhanced with Smart Citizen v1.3.0";
        QTest::newRow("this app") << "| Localizations Enhanced with Smart Citizen++ v0.0.1";
        QTest::newRow("this app, current form") << R"(\nLocalizations Enhanced with Smart Citizen++ v0.0.1)";
        QTest::newRow("extra space") << "  |  Localizations Enhanced with Smart Citizen v1.3.1   ";
    }

    void frontendStampStripsLegacyForms()
    {
        QFETCH(QString, suffix);
        IniMap m;
        m.insert(kFrontendVersionKey, QStringLiteral("Star Citizen Alpha 4.8.0 PTU ") + suffix);
        stampFrontendVersion(m, QStringLiteral("Smart Citizen++"), QStringLiteral("0.1.0"));
        QCOMPARE(frontend(m),
                 QStringLiteral(
                     R"(Star Citizen Alpha 4.8.0 PTU\nLocalizations Enhanced with Smart Citizen++ v0.1.0)"));
    }

    void frontendStampKeepsOrdinaryPipes()
    {
        IniMap m = ini({{"Frontend_PU_Version", "Star Citizen | Build A"}});
        stampFrontendVersion(m, kApp, QStringLiteral("1.3.1"));
        QCOMPARE(
            frontend(m),
            QStringLiteral(R"(Star Citizen | Build A\nLocalizations Enhanced with Smart Citizen v1.3.1)"));
    }

    void journalStamp()
    {
        const IniMap stock = ini({{"Journal_A_Content", "Stock text"},
                                  {"Journal_B_Content", "Stock B"},
                                  {"Journal_A_Title", "Title"}});
        IniMap m = ini({{"Journal_A_Content", "Stock text"}, // unchanged: no stamp
                        {"Journal_B_Content", "Edited B"},   // changed: stamped
                        {"Journal_New_Content", "Added"},    // not in stock: stamped
                        {"Journal_A_Title", "Edited title"}, // title keys never stamped
                        {"Journal_C_From,P", "Sender"},      // nor "From" lines
                        {"ui_Thing", "Not a journal"}});
        stampJournalEntries(m, stock, kApp, QStringLiteral("2.3.1"));
        const QString stamp = QStringLiteral(R"(\n\n[Edited with Smart Citizen v2.3.1])");
        QCOMPARE(m.value(QStringLiteral("Journal_A_Content")), QStringLiteral("Stock text"));
        QCOMPARE(m.value(QStringLiteral("Journal_B_Content")), QStringLiteral("Edited B") + stamp);
        QCOMPARE(m.value(QStringLiteral("Journal_New_Content")), QStringLiteral("Added") + stamp);
        QCOMPARE(m.value(QStringLiteral("Journal_A_Title")), QStringLiteral("Edited title"));
        QCOMPARE(m.value(QStringLiteral("Journal_C_From,P")), QStringLiteral("Sender"));
        QCOMPARE(m.value(QStringLiteral("ui_Thing")), QStringLiteral("Not a journal"));

        // Idempotent, and a version bump rolls the stamp forward.
        stampJournalEntries(m, stock, kApp, QStringLiteral("2.3.1"));
        QCOMPARE(m.value(QStringLiteral("Journal_B_Content")), QStringLiteral("Edited B") + stamp);
        stampJournalEntries(m, stock, kApp, QStringLiteral("2.4.0"));
        QCOMPARE(m.value(QStringLiteral("Journal_B_Content")),
                 QStringLiteral(R"(Edited B\n\n[Edited with Smart Citizen v2.4.0])"));

        // A stamped value that matches stock once unstamped loses the stamp.
        IniMap back = ini({{"Journal_A_Content", R"(Stock text\n\n[Edited with Smart Citizen v1.0])"}});
        stampJournalEntries(back, stock, kApp, QStringLiteral("2.3.1"));
        QCOMPARE(back.value(QStringLiteral("Journal_A_Content")), QStringLiteral("Stock text"));
    }

    // --- validator ------------------------------------------------------------

    void validator()
    {
        QTemporaryDir dir;
        const QString good = dir.filePath(QStringLiteral("good.ini"));
        writeFile(good, kBom + "key1=a\r\nkey2=b\r\n");
        const QSet<QString> stock = {QStringLiteral("key1"), QStringLiteral("key2")};
        QVERIFY(validateGameFile(good, stock).isEmpty());

        const QString noBom = dir.filePath(QStringLiteral("nobom.ini"));
        writeFile(noBom, "key1=a\r\nkey2=b\r\n");
        QVERIFY(validateGameFile(noBom, stock).contains(QStringLiteral("BOM")));

        const QString missing = dir.filePath(QStringLiteral("missing.ini"));
        writeFile(missing, kBom + "key1=a\r\nextra=x\r\n");
        const QString report = validateGameFile(missing, stock);
        QVERIFY(report.contains(QStringLiteral("1 key(s) from base.ini are missing")));
        QVERIFY(report.contains(QStringLiteral("  key2")));
        QVERIFY(report.contains(QStringLiteral("1 unexpected key(s)")));
        QVERIFY(report.contains(QStringLiteral("  extra")));

        QSet<QString> many;
        for (int i = 0; i < 25; ++i)
            many.insert(QStringLiteral("k%1").arg(i, 2, 10, QChar(u'0')));
        QVERIFY(validateGameFile(good, many).contains(QStringLiteral("... and 5 more")));

        QVERIFY(validateGameFile(dir.filePath(QStringLiteral("nope.ini")), stock).isEmpty());
    }

    // --- backups ----------------------------------------------------------------

    void backupsRotateAtFive()
    {
        QTemporaryDir dir;
        const QString game = dir.filePath(QStringLiteral("global.ini"));
        const GameFileBackups backups(dir.filePath(QStringLiteral("backups")));
        QVERIFY(backups.backup(game).isEmpty()); // nothing to back up yet

        QDateTime when(QDate(2026, 1, 1), QTime(12, 0, 0));
        QStringList made;
        for (int i = 0; i < 7; ++i) {
            writeFile(game, QByteArray("v=") + QByteArray::number(i));
            // Distinct mtimes so "oldest" is well defined.
            QFile f(game);
            if (!f.open(QIODevice::ReadWrite))
                qFatal("cannot open %s", qPrintable(f.fileName()));
            f.setFileTime(when.addSecs(i), QFileDevice::FileModificationTime);
            f.close();
            made << backups.backup(game, nullptr, when.addSecs(i));
            QVERIFY(QFileInfo::exists(made.last()));
        }
        const QFileInfoList list = backups.list();
        QCOMPARE(list.size(), GameFileBackups::kKeep);
        QCOMPARE(list.first().fileName(), QStringLiteral("global.ini.bak_20260101_120006"));
        QVERIFY(!QFileInfo::exists(made[0]));
        QVERIFY(!QFileInfo::exists(made[1]));

        QVERIFY(GameFileBackups::restore(made[3], game).isEmpty());
        QCOMPARE(readAll(game), QByteArray("v=3"));
    }

    // --- user.cfg ---------------------------------------------------------------

    void userCfgCreatesAndAppends()
    {
        QTemporaryDir dir;
        QCOMPARE(ensureUserCfgLanguage(dir.path(), QStringLiteral("english")), UserCfgResult::Created);
        QCOMPARE(readAll(dir.filePath(QStringLiteral("user.cfg"))), QByteArray("g_language = english\r\n"));

        QTemporaryDir other;
        writeFile(other.filePath(QStringLiteral("user.cfg")), "r_VSync = 1\n");
        QCOMPARE(ensureUserCfgLanguage(other.path(), QStringLiteral("english")), UserCfgResult::Added);
        QCOMPARE(readAll(other.filePath(QStringLiteral("user.cfg"))),
                 QByteArray("r_VSync = 1\r\n\r\ng_language = english\r\n"));
    }

    void userCfgNoSpuriousBlankLine()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("user.cfg")), "r_VSync = 1\n\n");
        ensureUserCfgLanguage(dir.path(), QStringLiteral("english"));
        QVERIFY(!readAll(dir.filePath(QStringLiteral("user.cfg"))).contains("\r\n\r\n\r\n"));
    }

    void userCfgRecognisesExistingLine_data()
    {
        QTest::addColumn<QString>("line");
        for (const char *line : {"g_language = english", "g_language=english", "g_language= english",
                                 "g_language =english", "g_language  =  english", "G_Language = english",
                                 "g_language = English", "g_language = ENGLISH", "g_language = \"english\"",
                                 "g_language = english   ", "g_language = english ; comment"})
            QTest::newRow(line) << QString::fromLatin1(line);
    }

    void userCfgRecognisesExistingLine()
    {
        QFETCH(QString, line);
        QTemporaryDir dir;
        const QByteArray original = ("r_VSync = 1\n" + line + "\nr_Width = 1920\n").toUtf8();
        writeFile(dir.filePath(QStringLiteral("user.cfg")), original);
        QCOMPARE(ensureUserCfgLanguage(dir.path(), QStringLiteral("english")), UserCfgResult::Unchanged);
        QCOMPARE(readAll(dir.filePath(QStringLiteral("user.cfg"))), original); // untouched
    }

    void userCfgUpdatesOtherLanguage()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("user.cfg")), "a = 1\nG_LANGUAGE = english\nb = 2\n");
        QCOMPARE(ensureUserCfgLanguage(dir.path(), QStringLiteral("portuguese_(brazil)")),
                 UserCfgResult::Updated);
        QCOMPARE(readAll(dir.filePath(QStringLiteral("user.cfg"))),
                 QByteArray("a = 1\r\ng_language = portuguese_(brazil)\r\nb = 2\r\n"));
        QCOMPARE(ensureUserCfgLanguage(dir.filePath(QStringLiteral("nope")), QStringLiteral("english")),
                 UserCfgResult::Failed);
    }

    // --- the whole apply --------------------------------------------------------

    void appliesToFakeInstall()
    {
        QTemporaryDir dir;
        const QString base = dir.filePath(QStringLiteral("data/LIVE/cache/base.ini"));
        writeFile(base, kBom + "Frontend_PU_Version=Star Citizen 4.9\r\n"
                               "ui_Button=OK\r\n"
                               "item_Name_SHLD_A=Shield\r\n"
                               "item_NameSHLDa=Shield\r\n"
                               "Journal_X_Content=Stock\r\n");
        const QString channel = dir.filePath(QStringLiteral("StarCitizen/LIVE"));
        QDir().mkpath(channel);
        const QString game = channel + QStringLiteral("/data/Localization/english/global.ini");
        writeFile(game, "old=content\r\n");

        ApplyInputs in;
        in.sources.sources[kSourceGlobal] = loadIni(base);
        in.sources.sources[kSourceEnhancements] = ini({{"item_NameSHLDa", "[SHLD-S1] Shield"}});
        in.sources.hierarchy = {kSourceGlobal, kSourceEnhancements, kSourceUser};
        in.userOverrides = ini({{"ui_Button", "Okay"}, {"Journal_X_Content", "Mine"}});
        in.baseIniPath = base;
        in.gameFile = game;
        in.backupsDir = dir.filePath(QStringLiteral("data/LIVE/backups"));
        in.channelInstallDir = channel;
        in.scLanguageId = QStringLiteral("english");
        in.appName = QStringLiteral("SCX");
        in.version = QStringLiteral("0.1.0");
        bool hookRan = false;
        in.beforeStamps = [&hookRan](IniMap &) { hookRan = true; };

        const ApplyOutcome out = applyToGame(in);
        QVERIFY2(out.ok, qPrintable(out.error + out.validation));
        QVERIFY(hookRan);
        QCOMPARE(readAll(out.backupPath), QByteArray("old=content\r\n"));
        QCOMPARE(out.userCfg, UserCfgResult::Created);
        QCOMPARE(
            readAll(game),
            kBom +
                QByteArray("Frontend_PU_Version=Star Citizen 4.9\\nLocalizations Enhanced with SCX v0.1.0\r\n"
                           "ui_Button=Okay\r\n"
                           "item_Name_SHLD_A=[SHLD-S1] Shield\r\n"
                           "item_NameSHLDa=[SHLD-S1] Shield\r\n"
                           "Journal_X_Content=Mine\\n\\n[Edited with SCX v0.1.0]\r\n"));
    }

    void rejectedFileRollsBack()
    {
        QTemporaryDir dir;
        const QString base = dir.filePath(QStringLiteral("base.ini"));
        writeFile(base, "a=1\r\nb=2\r\n");
        const QString game = dir.filePath(QStringLiteral("game/global.ini"));
        writeFile(game, "previous=good\r\n");

        ApplyInputs in;
        // Stock claims a key base.ini lacks, so validation must fail.
        in.sources.sources[kSourceGlobal] = ini({{"a", "1"}, {"b", "2"}, {"c", "3"}});
        in.sources.hierarchy = {kSourceGlobal};
        in.baseIniPath = base;
        in.gameFile = game;
        in.backupsDir = dir.filePath(QStringLiteral("backups"));
        in.appName = kApp;
        in.version = QStringLiteral("1");

        const ApplyOutcome out = applyToGame(in);
        QVERIFY(!out.ok);
        QVERIFY(out.validation.contains(QStringLiteral("  c")));
        QVERIFY(out.restoredBackup);
        QCOMPARE(readAll(game), QByteArray("previous=good\r\n"));
    }

    void overridesFromEntries()
    {
        QList<StringEntry> entries(3);
        entries[0].key = QStringLiteral("b");
        entries[0].customValue = QStringLiteral("x");
        entries[1].key = QStringLiteral("a"); // no custom value
        entries[2].key = QStringLiteral("c");
        entries[2].customValue = QStringLiteral("same");
        entries[2].originalValue = QStringLiteral("same"); // still an override, like the Python
        const IniMap m = userOverridesFrom(entries);
        QCOMPARE(m.size(), 2);
        QCOMPARE(m.entries().first().first, QStringLiteral("b"));
    }
};

QTEST_GUILESS_MAIN(TestApply)
#include "tst_apply.moc"
