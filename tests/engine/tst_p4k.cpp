#include "P4kBuilder.h"
#include "engine/cryxml/CryXml.h"
#include "engine/p4k/Archive.h"
#include "engine/p4k/Extractor.h"
#include "engine/zip/ZipWriter.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace engine;
using testing::bytesOf;
using testing::P4kBuilder;
using testing::P4kEntry;

namespace {

std::vector<std::uint8_t> pattern(std::size_t size, std::uint32_t seed)
{
    std::vector<std::uint8_t> out(size);
    std::uint32_t x = seed;
    for (auto &b : out) {
        x = x * 1664525u + 1013904223u;
        b = static_cast<std::uint8_t>((x >> 24) % 26 + 'a'); // compressible but not trivial
    }
    return out;
}

std::filesystem::path toPath(const QString &s)
{
    return std::filesystem::path(s.toStdWString());
}

std::filesystem::path writeFile(const QTemporaryDir &dir, const QString &name,
                                const std::vector<std::uint8_t> &bytes)
{
    QFile f(dir.filePath(name));
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(reinterpret_cast<const char *>(bytes.data()), static_cast<qint64>(bytes.size()));
    return toPath(f.fileName());
}

std::vector<std::uint8_t> readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.readAll();
    return {data.begin(), data.end()};
}

} // namespace

class TestP4k : public QObject
{
    Q_OBJECT

    QTemporaryDir dir_;
    std::filesystem::path archivePath_;
    std::vector<P4kEntry> entries_;

private slots:
    void initTestCase()
    {
        QVERIFY(dir_.isValid());
        entries_ = {
            {"Data/Localization/english/global.ini", bytesOf("key=Value\r\nother=Text\r\n"), 100, false},
            {"Data/Scripts/Secret.xml", bytesOf("<Secret value=\"1\" />"), 100, true},
            {"Data/plain.txt", bytesOf("stored bytes"), 0, false},
            {"Data/deflated.txt", pattern(5000, 7), 8, false},
            {"Data/big.bin", pattern(3 * 1024 * 1024 + 123, 42), 100, false},
            {"Data/big_crypt.bin", pattern(2 * 1024 * 1024 + 7, 9), 100, true},
            {"Data/crypt_stored.txt", bytesOf("encrypted but stored"), 0, true},
            {"Data/empty.dat", {}, 100, false},
        };
        P4kBuilder builder;
        for (const auto &e : entries_)
            builder.add(e);
        archivePath_ = writeFile(dir_, "Data.p4k", builder.build());
        QVERIFY(!archivePath_.empty());
    }

    void opensCigArchive()
    {
        auto archive = p4k::Archive::open(archivePath_);
        QVERIFY2(archive, archive ? "" : archive.error().message.c_str());
        QCOMPARE((*archive)->entryCount(), entries_.size());
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            QCOMPARE(std::string((*archive)->name(i)), entries_[i].name); // backslashes become '/'
            const auto &e = (*archive)->entry(i);
            QCOMPARE(e.method, entries_[i].method);
            QCOMPARE(e.encrypted, entries_[i].encrypted);
            QCOMPARE(e.uncompressedSize, entries_[i].content.size());
        }
    }

    void decodesEveryEntry()
    {
        auto archive = *p4k::Archive::open(archivePath_);
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            auto data = archive->read(i);
            QVERIFY2(data, data ? "" : data.error().message.c_str());
            QVERIFY2(*data == entries_[i].content, entries_[i].name.c_str());
        }
    }

    void storedHashMatchesStoredBytes()
    {
        auto archive = *p4k::Archive::open(archivePath_);
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            crypto::Sha256 sha;
            QVERIFY(archive->readStored(i, [&](std::span<const std::uint8_t> b) -> Result<void> {
                sha.update(b);
                return {};
            }));
            auto expected = archive->storedHash(i);
            QVERIFY(expected && *expected);
            QVERIFY(sha.finish() == **expected);
        }
    }

    void findIsCaseInsensitive()
    {
        auto archive = *p4k::Archive::open(archivePath_);
        QCOMPARE(archive->find("data/localization/ENGLISH/global.ini").value_or(99), std::size_t(0));
        QCOMPARE(archive->find(R"(DATA\Scripts\secret.XML)").value_or(99), std::size_t(1));
        QVERIFY(!archive->find("Data/missing.txt"));
    }

    void zstdMethodWithoutMagicPassesThrough()
    {
        // unp4k returns method-100 data as-is when it is not a zstd frame.
        QTemporaryDir dir;
        P4kEntry raw{"Data/raw.txt", bytesOf("not compressed at all"), 0, false};
        P4kBuilder builder;
        builder.add(raw);
        auto bytes = builder.build();
        // Relabel the entry as ZSTD in both headers (method field at +8 / +10).
        bytes[8] = 100;
        const std::size_t cd = bytes.size() - 828 - 22 - 16 - 20 - 56 - (46 + raw.name.size() + 206);
        QCOMPARE(bytes[cd], std::uint8_t('P'));
        bytes[cd + 10] = 100;
        const auto path = writeFile(dir, "raw.p4k", bytes);
        auto archive = *p4k::Archive::open(path);
        QCOMPARE(archive->entry(0).method, std::uint16_t(100));
        QVERIFY(*archive->read(0) == raw.content);
    }

    void reportsCorruptData()
    {
        // Entry data starts after the 30-byte local header, the name and a
        // 20-byte ZIP64 extra field.
        const std::string name = "Data/x.bin";
        const std::size_t dataAt = 30 + name.size() + 20;
        P4kBuilder builder;
        builder.add({name, pattern(10000, 3), 100, false});
        const auto good = builder.build();

        // A broken frame header fails to decode.
        {
            QTemporaryDir dir;
            auto bytes = good;
            bytes[dataAt + 4] = 0xFF; // frame descriptor with the reserved bit set
            auto archive = *p4k::Archive::open(writeFile(dir, "bad.p4k", bytes));
            auto data = archive->read(0);
            QVERIFY(!data);
            QCOMPARE(data.error().code, Errc::Corrupt);
        }
        // zstd frames here carry no checksum (and CIG's CRC field is not a
        // content CRC), so damaged payload bytes may still decode. The stored
        // SHA-256 is what catches them.
        {
            QTemporaryDir dir;
            auto bytes = good;
            for (std::size_t i = dataAt + 100; i < dataAt + 140; ++i)
                bytes[i] ^= 0x5A;
            auto archive = *p4k::Archive::open(writeFile(dir, "bad.p4k", bytes));
            crypto::Sha256 sha;
            QVERIFY(archive->readStored(0, [&](std::span<const std::uint8_t> b) -> Result<void> {
                sha.update(b);
                return {};
            }));
            QVERIFY(sha.finish() != **archive->storedHash(0));
        }
    }

    void rejectsNonArchives()
    {
        QTemporaryDir dir;
        const auto path = writeFile(dir, "junk.p4k", pattern(5000, 1));
        auto archive = p4k::Archive::open(path);
        QVERIFY(!archive);
        QCOMPARE(archive.error().code, Errc::Format);

        auto missing = p4k::Archive::open(toPath(dir.filePath("nope.p4k")));
        QVERIFY(!missing);
        QCOMPARE(missing.error().code, Errc::NotFound);
    }

#ifdef _WIN32
    // The RSI Launcher holds Data.p4k open for writing while it patches.
    void lockedArchiveReportsLocked()
    {
        HANDLE writer = CreateFileW(archivePath_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(writer != INVALID_HANDLE_VALUE);
        auto archive = p4k::Archive::open(archivePath_);
        CloseHandle(writer);
        QVERIFY(!archive);
        QCOMPARE(archive.error().code, Errc::Locked);
    }
#endif

    void unp4kFilter_data()
    {
        QTest::addColumn<QString>("filter");
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("matches");
        QTest::newRow("substring") << "global.ini" << "Data/Localization/german_(germany)/global.ini" << true;
        QTest::newRow("case") << "GLOBAL.INI" << "Data/Localization/english/global.ini" << true;
        QTest::newRow("dcb") << ".dcb" << "Data/Game2.dcb" << true;
        QTest::newRow("star ext is substring") << "*.xml" << "Data/a.xml.bak" << true;
        QTest::newRow("xml filter adds dcb") << "*.xml" << "Data/Game2.dcb" << true;
        QTest::newRow("star") << "*" << "anything" << true;
        QTest::newRow("dot star") << ".*" << "anything" << true;
        QTest::newRow("miss") << "global.ini" << "Data/Game2.dcb" << false;
    }

    void unp4kFilter()
    {
        QFETCH(QString, filter);
        QFETCH(QString, name);
        QFETCH(bool, matches);
        QCOMPARE(p4k::unp4kFilterMatches(filter.toStdString(), name.toStdString()), matches);
    }

    void glob_data()
    {
        QTest::addColumn<QString>("pattern");
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("matches");
        QTest::newRow("dcb") << "Data/Game*.dcb" << "Data/Game2.dcb" << true;
        QTest::newRow("case") << "data/game*.DCB" << "Data/Game2.dcb" << true;
        QTest::newRow("deep star") << "Data/*.xml" << "Data/Scripts/a/b.xml" << true;
        QTest::newRow("question") << "Data/Game?.dcb" << "Data/Game2.dcb" << true;
        QTest::newRow("question needs one") << "Data/Game?.dcb" << "Data/Game.dcb" << false;
        QTest::newRow("exact") << "Data/Localization/english/global.ini"
                               << "Data/Localization/english/global.ini" << true;
        QTest::newRow("anchored") << "Data/Localization/english/global.ini"
                                  << "Data/Localization/english/global.ini.bak" << false;
        QTest::newRow("backslash") << R"(Data\Game*.dcb)" << "Data/Game2.dcb" << true;
    }

    void glob()
    {
        QFETCH(QString, pattern);
        QFETCH(QString, name);
        QFETCH(bool, matches);
        QCOMPARE(p4k::globMatches(pattern.toStdString(), name.toStdString()), matches);
    }

    void extractsInParallel()
    {
        auto archive = *p4k::Archive::open(archivePath_);
        QTemporaryDir out;
        const auto entries = p4k::selectEntries(*archive, [](std::string_view) { return true; });
        p4k::ExtractOptions options;
        options.outputDir = toPath(out.path());
        options.threads = 3;
        auto stats = p4k::extract(*archive, entries, options);
        QVERIFY(stats);
        QVERIFY(stats->failures.empty());
        QCOMPARE(stats->extracted, entries_.size());
        for (const auto &e : entries_)
            QVERIFY2(readFile(out.filePath(QString::fromStdString(e.name))) == e.content, e.name.c_str());

        // Nothing half-written is left behind.
        QDirIterator it(out.path(), QStringList{"*.part"}, QDir::Files, QDirIterator::Subdirectories);
        QVERIFY(!it.hasNext());

        // A second run skips what is already there.
        options.skipExisting = true;
        stats = p4k::extract(*archive, entries, options);
        QVERIFY(stats);
        QCOMPARE(stats->skipped, entries_.size());
        QCOMPARE(stats->extracted, std::size_t(0));
    }

    void extractTransformsContent()
    {
        auto archive = *p4k::Archive::open(archivePath_);
        QTemporaryDir out;
        const auto entries = p4k::selectEntries(
            *archive, [](std::string_view name) { return p4k::unp4kFilterMatches("*.ini", name); });
        QCOMPARE(entries.size(), std::size_t(1));
        p4k::ExtractOptions options;
        options.outputDir = toPath(out.path());
        options.transform = [](std::string_view,
                               std::vector<std::uint8_t> bytes) -> Result<std::vector<std::uint8_t>> {
            bytes.insert(bytes.begin(), {'#', ' '});
            return bytes;
        };
        auto stats = p4k::extract(*archive, entries, options);
        QVERIFY(stats && stats->failures.empty());
        QCOMPARE(readFile(out.filePath("Data/Localization/english/global.ini")),
                 bytesOf("# key=Value\r\nother=Text\r\n"));
    }

    void zipWriterRoundTrip()
    {
        QTemporaryDir dir;
        const auto path = toPath(dir.filePath("pack.zip"));
        const auto big = pattern(70000, 5);
        {
            auto zip = zip::ZipWriter::create(path);
            QVERIFY(zip);
            QVERIFY(zip->add("global.ini", std::string_view("a=1\r\n")));
            QVERIFY(zip->add("dir/stored.bin", big, 0));
            QVERIFY(zip->add("dir/deflated.bin", big, 9));
            QVERIFY(!QFile::exists(dir.filePath("pack.zip"))); // only appears on finish()
            QVERIFY(zip->finish());
        }
        auto archive = p4k::Archive::open(path);
        QVERIFY2(archive, archive ? "" : archive.error().message.c_str());
        QCOMPARE((*archive)->entryCount(), std::size_t(3));
        QCOMPARE(*(*archive)->read(0), bytesOf("a=1\r\n"));
        QVERIFY(*(*archive)->read(1) == big);
        QVERIFY(*(*archive)->read(2) == big);
        QVERIFY((*archive)->entry(2).compressedSize < big.size());
        QCOMPARE((*archive)->entry(0).method, std::uint16_t(8));
    }
};

QTEST_GUILESS_MAIN(TestP4k)
#include "tst_p4k.moc"
