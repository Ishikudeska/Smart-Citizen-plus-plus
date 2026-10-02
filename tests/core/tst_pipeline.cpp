// Extraction (base.ini + DataForge cache) and the DataForge patcher. Ports
// test_dataforge_patcher.py and the freshness cases of test_pak_extraction.py.

#include "../engine/DcbBuilder.h"
#include "../engine/P4kBuilder.h"

#include "core/pipeline/Extraction.h"
#include "core/pipeline/Patcher.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace core;
using testing::DataType;
using testing::DcbBuilder;
using testing::Inst;

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

// Two records in a kept subtree, one outside it.
std::vector<std::uint8_t> buildDcb()
{
    DcbBuilder b;
    const auto info = b.addStruct("Info");
    b.addProperty(info, "name", DataType::String);
    const auto holder = b.addStruct("Holder");
    b.addProperty(holder, "info", DataType::Class, testing::Conversion::Attribute, static_cast<std::uint16_t>(info));
    b.addInstance(holder, Inst().u32(b.text("old")));
    b.addInstance(holder, Inst().u32(b.text("other")));
    b.addRecord("Holder.Kept", "libs/foundry/records/entities/scitem/ships/kept.xml", holder, 0, testing::makeGuid(1));
    b.addRecord("Holder.Mission", "libs/foundry/records/missionbroker/pu_missions/m.xml", holder, 1,
                testing::makeGuid(2));
    b.addRecord("Holder.Dropped", "libs/foundry/records/tagdatabase/dropped.xml", holder, 1, testing::makeGuid(3));
    return b.build();
}

QString buildP4k(const QTemporaryDir &dir)
{
    testing::P4kBuilder builder;
    builder.add({"Data/Localization/english/global.ini", testing::bytesOf("\xEF\xBB\xBFkey=Value\r\n"), 100, false});
    builder.add({"Data/Localization/german_(germany)/global.ini", testing::bytesOf("key=Wert\r\n"), 100, false});
    builder.add({"Data/Game2.dcb", buildDcb(), 100, false});
    const auto bytes = builder.build();
    const QString path = dir.filePath(QStringLiteral("StarCitizen/LIVE/Data.p4k"));
    writeFile(path, QByteArray(reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size())));
    return path;
}

std::shared_ptr<const engine::p4k::Archive> open(const QString &path)
{
    auto archive = engine::p4k::Archive::open(std::filesystem::path(path.toStdU16String()));
    return archive ? *archive : nullptr;
}

const QByteArray kKeptXml = "<Holder.Kept __type=\"Holder\" __ref=\"08070605-0403-0201-100f-0e0d0c0b0a09\" "
                            "__path=\"libs/foundry/records/entities/scitem/ships/kept.xml\" __team=\"Unknown\">\r\n"
                            "  <info name=\"old\" />\r\n"
                            "</Holder.Kept>";

QString patch(const QString &xpath, const QString &expected, const QString &set)
{
    return QStringLiteral(R"({"xpath": "%1", "attribute": "name", "expected": "%2", "set": "%3"})")
        .arg(xpath, expected, set);
}

} // namespace

class TestPipeline : public QObject
{
    Q_OBJECT

private slots:
    void extractsBaseIniAndStamps()
    {
        QTemporaryDir dir;
        const QString p4k = buildP4k(dir);
        auto archive = open(p4k);
        QVERIFY(archive);
        const QString base = dir.filePath(QStringLiteral("data/LIVE/cache/base.ini"));
        QVERIFY(!baseIniIsFresh(p4k, base));
        QVERIFY(extractBaseIni(*archive, base));
        QCOMPARE(readAll(base), QByteArray("\xEF\xBB\xBFkey=Value\r\n")); // byte for byte, BOM included
        QVERIFY(baseIniIsFresh(p4k, base));

        // A patched (resized) Data.p4k makes it stale. The open archive
        // refuses writers, as the launcher would be refused, so close it.
        archive.reset();
        QFile f(p4k);
        QVERIFY(f.open(QIODevice::Append));
        f.write("x");
        f.close();
        QVERIFY(!baseIniIsFresh(p4k, base));
    }

    void buildsDataForgeCacheFromKeptSubtrees()
    {
        QTemporaryDir dir;
        const QString p4k = buildP4k(dir);
        auto archive = open(p4k);
        const QString cache = dir.filePath(QStringLiteral("Local/LIVE/cache/dataforge"));
        QVERIFY(!dataForgeCacheIsFresh(p4k, cache));

        QStringList steps;
        auto result = extractDataForge(*archive, cache, dir.filePath(QStringLiteral("no-patches")),
                                       [&steps](const QString &step, qint64, qint64) {
                                           if (!steps.contains(step))
                                               steps << step;
                                       });
        QVERIFY2(result, result ? "" : result.error().message.c_str());
        QCOMPARE(result->records, std::size_t(2));
        QVERIFY(steps.contains(QStringLiteral("Converting DataForge records")));

        const QString records = dataForgeRecordsDir(cache);
        QCOMPARE(readAll(records + QStringLiteral("/entities/scitem/ships/kept.xml")), kKeptXml);
        QVERIFY(QFileInfo::exists(records + QStringLiteral("/missionbroker/pu_missions/m.xml")));
        QVERIFY(!QFileInfo::exists(records + QStringLiteral("/tagdatabase/dropped.xml"))); // not a kept subtree
        QVERIFY(dataForgeCacheIsFresh(p4k, cache));
        QVERIFY(!QFileInfo::exists(cache + QStringLiteral("/raw.new")));
        QVERIFY(!QFileInfo::exists(cache + QStringLiteral("/raw.old")));

        // A rebuild replaces the tree (stale files from an older build go).
        writeFile(records + QStringLiteral("/entities/scitem/stale.xml"), "old build");
        QVERIFY(extractDataForge(*archive, cache, dir.filePath(QStringLiteral("no-patches"))));
        QVERIFY(!QFileInfo::exists(records + QStringLiteral("/entities/scitem/stale.xml")));

        // A stamp without content is not fresh.
        QVERIFY(QDir(cache + QStringLiteral("/raw")).removeRecursively());
        QVERIFY(!dataForgeCacheIsFresh(p4k, cache));
    }

    void exportsGameData()
    {
        QTemporaryDir dir;
        const auto archive = open(buildP4k(dir));
        QVERIFY(archive);
        const QString out = dir.filePath(QStringLiteral("export/game_data.json"));
        QStringList steps;
        const auto lines = exportGameData(*archive, out, {}, {}, QStringLiteral("LIVE"),
                                          [&](const QString &step, qint64, qint64) { steps << step; });
        QVERIFY2(lines, lines ? "" : lines.error().message.c_str());
        QVERIFY(lines->contains(QStringLiteral("Weapons: 0 (0 guns + 0 missiles)")));
        QCOMPARE(steps.size(), 3);
        const QByteArray json = readAll(out);
        QVERIFY(json.startsWith("{\r\n  \"schema_version\": 1,\r\n"));
        QVERIFY(json.contains("\"channel\": \"LIVE\""));
        QVERIFY(json.endsWith("\"racks\": []\r\n}\r\n"));
    }

    void patchesAreAppliedAndIdempotent()
    {
        QTemporaryDir dir;
        const QString records = dir.filePath(QStringLiteral("records"));
        writeFile(records + QStringLiteral("/entities/scitem/ships/kept.xml"), kKeptXml);
        const QString patches = dir.filePath(QStringLiteral("patches"));
        writeFile(patches + QStringLiteral("/entities/kept.patch.json"),
                  QStringLiteral(R"({"target": "entities/scitem/ships/kept.xml", "description": "test",
                                     "edits": [%1, %2, %3]})")
                      .arg(patch(QStringLiteral(".//info"), QStringLiteral("old"), QStringLiteral("new")),
                           patch(QStringLiteral(".//info"), QStringLiteral("something else"), QStringLiteral("x")),
                           patch(QStringLiteral(".//missing"), QStringLiteral("old"), QStringLiteral("new")))
                      .toUtf8());

        PatchReport report = applyPatches(patches, records);
        QCOMPARE(report.patchesSeen, 1);
        QCOMPARE(report.filesRewritten, 1);
        QCOMPARE(report.editsApplied, 1);
        QCOMPARE(report.editsMismatched, 1); // "new" is neither expected nor set: left alone
        QCOMPARE(report.editsUnmatched, 1);
        QVERIFY(report.errors.isEmpty());
        // Rewritten in unforge's format, only the value changed.
        QByteArray expected = kKeptXml;
        expected.replace("name=\"old\"", "name=\"new\"");
        QCOMPARE(readAll(records + QStringLiteral("/entities/scitem/ships/kept.xml")), expected);

        report = applyPatches(patches, records);
        QCOMPARE(report.editsApplied, 0);
        QCOMPARE(report.editsAlreadyApplied, 1);
        QCOMPARE(report.filesRewritten, 0);
    }

    void badPatchesAreReported()
    {
        QTemporaryDir dir;
        const QString patches = dir.filePath(QStringLiteral("patches"));
        writeFile(patches + QStringLiteral("/a.patch.json"), "{not json");
        writeFile(patches + QStringLiteral("/b.patch.json"), R"({"target": "missing.xml", "edits": [{}]})");
        writeFile(patches + QStringLiteral("/c.patch.json"), R"({"edits": []})");
        const PatchReport report = applyPatches(patches, dir.filePath(QStringLiteral("records")));
        QCOMPARE(report.patchesSeen, 3);
        QCOMPARE(report.errors.size(), 3);
    }

    void bundledPatchesParse()
    {
        // The real patch files shipped in resources/patches.
        const QString root = QStringLiteral(SC_SOURCE_DIR "/resources/patches");
        if (!QFileInfo::exists(root))
            QSKIP("resources/patches not present");
        QVERIFY(!loadLocstringWorkarounds(root).isEmpty());
        int count = 0;
        for (QDirIterator it(root, {QStringLiteral("*.patch.json")}, QDir::Files, QDirIterator::Subdirectories);
             it.hasNext(); it.next())
            ++count;
        QCOMPARE(count, 3);
    }

    void locstringWorkarounds()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("x.patch.json")),
                  R"({"target": "t.xml", "edits": [], "locstring_workarounds": [
                        {"target": "desc_a", "append_from": "desc_b", "separator": "\\n--\\n"},
                        {"target": "only_target"}]})");
        writeFile(dir.filePath(QStringLiteral("corrupt.patch.json")), "{\"locstring\xA0" "broken\"}");
        const auto workarounds = loadLocstringWorkarounds(dir.path());
        QCOMPARE(workarounds.size(), 1);

        IniMap entries;
        entries.insert(QStringLiteral("desc_a"), QStringLiteral("A"));
        entries.insert(QStringLiteral("desc_b"), QStringLiteral("B"));
        QCOMPARE(applyLocstringWorkarounds(entries, workarounds), 1);
        QCOMPARE(entries.value(QStringLiteral("desc_a")), QStringLiteral(R"(A\n--\nB)"));
        QCOMPARE(applyLocstringWorkarounds(entries, workarounds), 0); // already applied

        IniMap other;
        other.insert(QStringLiteral("desc_a"), QStringLiteral("A"));
        QCOMPARE(applyLocstringWorkarounds(other, workarounds), 0); // source key not here
    }
};

QTEST_GUILESS_MAIN(TestPipeline)
#include "tst_pipeline.moc"
