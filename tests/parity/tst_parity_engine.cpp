// Engine parity against the original tools' output (plan P1-P3).
//
// Needs a real Star Citizen install and the Smart Citizen caches produced
// from that same build:
//   SCX_P4K          path to Data.p4k (required; every test skips without it)
//   SCX_BASE_INI     unp4k's global.ini output, default
//                    %USERPROFILE%\Documents\Smart Citizen\LIVE\cache\base.ini
//   SCX_FORGE_CACHE  unforge's XML tree, default
//                    %LOCALAPPDATA%\Smart Citizen\LIVE\cache\dataforge\raw
//   SCX_PARITY_FULL  =1 to decode every entry in P2 (reads ~158 GB)
// After a game patch the caches are stale until Smart Citizen regenerates
// them, and P1/P3 fail until then.

#include "engine/Parallel.h"
#include "engine/forge/DataForge.h"
#include "engine/forge/RecordBuilder.h"
#include "engine/io/FileSystem.h"
#include "engine/io/RandomAccessFile.h"
#include "engine/p4k/Archive.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTest>

#include <algorithm>
#include <mutex>

using namespace engine;

namespace {

std::filesystem::path envPath(const char *name, const QString &fallback = {})
{
    const QString value = qEnvironmentVariable(name, fallback);
    return value.isEmpty() ? std::filesystem::path() : std::filesystem::path(value.toStdWString());
}

std::optional<std::string> readText(const std::filesystem::path &path)
{
    auto file = io::RandomAccessFile::open(path);
    if (!file)
        return std::nullopt;
    std::string bytes(static_cast<std::size_t>(file->size()), '\0');
    if (!file->readAt(0, std::span(reinterpret_cast<std::uint8_t *>(bytes.data()), bytes.size())))
        return std::nullopt;
    return bytes;
}

// The subtrees Smart Citizen caches (DATAFORGE_KEEP_SUBPATHS).
const char *const kKeepSubpaths[] = {
    "entities/scitem",
    "entities/spaceships",
    "entities/missions",
    "entities/contracts",
    "entities/jobterminal",
    "contracts/contractgenerator",
    "contracts/contracttemplates",
    "crafting/blueprintrewards",
    "crafting/blueprints/crafting",
    "missionbroker/pu_missions",
    "ammoparams/vehicle",
    "ammoparams/fps",
    "reputation/rewards/missionrewards_reputation",
    "reputation/standings",
};

bool kept(std::string_view path)
{
    return std::any_of(std::begin(kKeepSubpaths), std::end(kKeepSubpaths), [path](const char *sub) {
        const std::string prefix = std::string("libs/foundry/records/") + sub + "/";
        return path.starts_with(prefix);
    });
}

} // namespace

class TestParityEngine : public QObject
{
    Q_OBJECT

    std::shared_ptr<const p4k::Archive> archive_;

private slots:
    void initTestCase()
    {
        const auto p4kPath = envPath("SCX_P4K");
        if (p4kPath.empty())
            QSKIP("SCX_P4K is not set");
        auto archive = p4k::Archive::open(p4kPath);
        QVERIFY2(archive, archive ? "" : archive.error().message.c_str());
        archive_ = *archive;
    }

    // P1: global.ini equals unp4k's extraction byte for byte.
    void p1BaseIni()
    {
        const auto expectedPath =
            envPath("SCX_BASE_INI", QDir::homePath() + "/Documents/Smart Citizen/LIVE/cache/base.ini");
        const auto expected = readText(expectedPath);
        if (!expected)
            QSKIP("no unp4k base.ini to compare against (SCX_BASE_INI)");

        const auto index = archive_->find("Data/Localization/english/global.ini");
        QVERIFY(index);
        auto actual = archive_->read(*index);
        QVERIFY2(actual, actual ? "" : actual.error().message.c_str());
        QCOMPARE(actual->size(), expected->size());
        QVERIFY(std::equal(actual->begin(), actual->end(), expected->begin(),
                           [](std::uint8_t a, char b) { return a == static_cast<std::uint8_t>(b); }));
    }

    // P2: entries decode to their recorded size and their stored bytes
    // match CIG's SHA-256. Every encrypted entry plus a sample by default.
    void p2Entries()
    {
        const bool full = qEnvironmentVariable("SCX_PARITY_FULL") == "1";
        std::vector<std::size_t> picked;
        for (std::size_t i = 0; i < archive_->entryCount(); ++i)
            if (full || archive_->entry(i).encrypted || i % 997 == 0)
                picked.push_back(i);

        std::mutex mutex;
        std::vector<std::string> failures;
        parallelFor(
            picked.size(), 0,
            [&] {
                return [&](std::size_t n) {
                    const std::size_t i = picked[n];
                    std::string problem;
                    crypto::Sha256 sha;
                    auto stored =
                        archive_->readStored(i, [&](std::span<const std::uint8_t> b) -> Result<void> {
                            sha.update(b);
                            return {};
                        });
                    auto hash = archive_->storedHash(i);
                    auto decoded =
                        archive_->read(i, [](std::span<const std::uint8_t>) -> Result<void> { return {}; });
                    if (!stored || !hash || !decoded)
                        problem = (!stored ? stored.error() : !hash ? hash.error() : decoded.error()).message;
                    else if (!*hash || sha.finish() != **hash)
                        problem = "stored bytes do not match the SHA-256";
                    if (!problem.empty()) {
                        std::lock_guard lock(mutex);
                        failures.push_back(std::string(archive_->name(i)) + ": " + problem);
                    }
                };
            },
            nullptr, nullptr);
        qInfo("checked %zu entries", picked.size());
        for (std::size_t i = 0; i < std::min<std::size_t>(failures.size(), 10); ++i)
            qWarning("%s", failures[i].c_str());
        QCOMPARE(failures.size(), std::size_t(0));
    }

    // P3: every record unforge cached is reproduced byte for byte.
    void p3DataForge()
    {
        const auto cacheRoot =
            envPath("SCX_FORGE_CACHE", QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
                                           "/Smart Citizen/LIVE/cache/dataforge/raw");
        if (!io::exists(cacheRoot / "libs"))
            QSKIP("no unforge cache to compare against (SCX_FORGE_CACHE)");

        std::optional<std::size_t> dcb;
        for (std::size_t i = 0; i < archive_->entryCount() && !dcb; ++i)
            if (archive_->name(i).ends_with(".dcb"))
                dcb = i;
        QVERIFY(dcb);
        auto bytes = archive_->read(*dcb);
        QVERIFY(bytes);
        auto df = forge::DataForge::load(std::move(*bytes));
        QVERIFY2(df, df ? "" : df.error().message.c_str());

        std::vector<std::string> cached;
        QVERIFY(io::forEachFile(cacheRoot, [&](std::string_view path) {
            if (kept(path) && path.ends_with(".xml"))
                cached.emplace_back(path);
        }));
        QVERIFY(!cached.empty());

        std::mutex mutex;
        std::vector<std::string> failures;
        std::atomic<std::size_t> patched{0};
        parallelFor(
            cached.size(), 0,
            [&] {
                return [&, builder = forge::RecordBuilder(*df), tree = xml::XmlTree(),
                        actual = std::string()](std::size_t n) mutable {
                    const std::string &path = cached[n];
                    auto expected = readText(
                        cacheRoot / std::filesystem::path(std::u8string(
                                        reinterpret_cast<const char8_t *>(path.data()), path.size())));
                    if (expected && expected->starts_with("<?xml")) {
                        patched.fetch_add(1); // rewritten by Smart Citizen's patcher (lxml)
                        return;
                    }
                    // unforge's JIT sometimes adds one stack frame to the error text.
                    static constexpr std::string_view kExtraFrame =
                        "&#xD;&#xA;   at unforge.DataForge.ReadDataMappingAtIndex(Int64 index)";
                    for (auto p = expected ? expected->find(kExtraFrame) : std::string::npos;
                         p != std::string::npos; p = expected->find(kExtraFrame, p))
                        expected->erase(p, kExtraFrame.size());

                    const auto index = df->recordByPath(path);
                    std::string problem;
                    if (!expected)
                        problem = "unreadable";
                    else if (!index)
                        problem = "no such record";
                    else if (!forge::writeRecordXml(builder, tree, *index, actual))
                        problem = "rendered empty";
                    else if (actual != *expected)
                        problem = "differs";
                    if (!problem.empty()) {
                        std::lock_guard lock(mutex);
                        failures.push_back(path + ": " + problem);
                    }
                };
            },
            nullptr, nullptr);

        // Records the cache should hold but doesn't.
        std::sort(cached.begin(), cached.end());
        forge::RecordBuilder builder(*df);
        xml::XmlTree tree;
        std::string out;
        for (const auto index : df->fileRecords()) {
            const std::string path(df->recordFileName(index));
            if (kept(path) && !std::binary_search(cached.begin(), cached.end(), path) &&
                forge::writeRecordXml(builder, tree, index, out))
                failures.push_back(path + ": missing from the cache");
        }

        qInfo("compared %zu cached records (%zu patched by Smart Citizen, skipped)", cached.size(),
              patched.load());
        std::sort(failures.begin(), failures.end());
        for (std::size_t i = 0; i < std::min<std::size_t>(failures.size(), 10); ++i)
            qWarning("%s", failures[i].c_str());
        QCOMPARE(failures.size(), std::size_t(0));
    }
};

QTEST_GUILESS_MAIN(TestParityEngine)
#include "tst_parity_engine.moc"
